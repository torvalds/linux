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

#include <linux/device.h>
#include "scic_controller.h"
#include "scic_phy.h"
#include "scic_port.h"
#include "scic_remote_device.h"
#include "scic_sds_controller.h"
#include "scic_sds_controller_registers.h"
#include "scic_sds_pci.h"
#include "scic_sds_phy.h"
#include "scic_sds_port_configuration_agent.h"
#include "scic_sds_port.h"
#include "scic_sds_remote_device.h"
#include "scic_sds_request.h"
#include "sci_environment.h"
#include "sci_util.h"
#include "scu_completion_codes.h"
#include "scu_constants.h"
#include "scu_event_codes.h"
#include "scu_remote_node_context.h"
#include "scu_task_context.h"
#include "scu_unsolicited_frame.h"

#define SCU_CONTEXT_RAM_INIT_STALL_TIME      200

/**
 * smu_dcc_get_max_ports() -
 *
 * This macro returns the maximum number of logical ports supported by the
 * hardware. The caller passes in the value read from the device context
 * capacity register and this macro will mash and shift the value appropriately.
 */
#define smu_dcc_get_max_ports(dcc_value) \
	(\
		(((dcc_value) & SMU_DEVICE_CONTEXT_CAPACITY_MAX_LP_MASK) \
		 >> SMU_DEVICE_CONTEXT_CAPACITY_MAX_LP_SHIFT) + 1 \
	)

/**
 * smu_dcc_get_max_task_context() -
 *
 * This macro returns the maximum number of task contexts supported by the
 * hardware. The caller passes in the value read from the device context
 * capacity register and this macro will mash and shift the value appropriately.
 */
#define smu_dcc_get_max_task_context(dcc_value)	\
	(\
		(((dcc_value) & SMU_DEVICE_CONTEXT_CAPACITY_MAX_TC_MASK) \
		 >> SMU_DEVICE_CONTEXT_CAPACITY_MAX_TC_SHIFT) + 1 \
	)

/**
 * smu_dcc_get_max_remote_node_context() -
 *
 * This macro returns the maximum number of remote node contexts supported by
 * the hardware. The caller passes in the value read from the device context
 * capacity register and this macro will mash and shift the value appropriately.
 */
#define smu_dcc_get_max_remote_node_context(dcc_value) \
	(\
		(((dcc_value) & SMU_DEVICE_CONTEXT_CAPACITY_MAX_RNC_MASK) \
		 >> SMU_DEVICE_CONTEXT_CAPACITY_MAX_RNC_SHIFT) + 1 \
	)


static void scic_sds_controller_power_control_timer_handler(
	void *controller);
#define SCIC_SDS_CONTROLLER_MIN_TIMER_COUNT  3
#define SCIC_SDS_CONTROLLER_MAX_TIMER_COUNT  3

/**
 *
 *
 * The number of milliseconds to wait for a phy to start.
 */
#define SCIC_SDS_CONTROLLER_PHY_START_TIMEOUT      100

/**
 *
 *
 * The number of milliseconds to wait while a given phy is consuming power
 * before allowing another set of phys to consume power. Ultimately, this will
 * be specified by OEM parameter.
 */
#define SCIC_SDS_CONTROLLER_POWER_CONTROL_INTERVAL 500

/**
 * COMPLETION_QUEUE_CYCLE_BIT() -
 *
 * This macro will return the cycle bit of the completion queue entry
 */
#define COMPLETION_QUEUE_CYCLE_BIT(x) ((x) & 0x80000000)

/**
 * NORMALIZE_GET_POINTER() -
 *
 * This macro will normalize the completion queue get pointer so its value can
 * be used as an index into an array
 */
#define NORMALIZE_GET_POINTER(x) \
	((x) & SMU_COMPLETION_QUEUE_GET_POINTER_MASK)

/**
 * NORMALIZE_PUT_POINTER() -
 *
 * This macro will normalize the completion queue put pointer so its value can
 * be used as an array inde
 */
#define NORMALIZE_PUT_POINTER(x) \
	((x) & SMU_COMPLETION_QUEUE_PUT_POINTER_MASK)


/**
 * NORMALIZE_GET_POINTER_CYCLE_BIT() -
 *
 * This macro will normalize the completion queue cycle pointer so it matches
 * the completion queue cycle bit
 */
#define NORMALIZE_GET_POINTER_CYCLE_BIT(x) \
	((SMU_CQGR_CYCLE_BIT & (x)) << (31 - SMU_COMPLETION_QUEUE_GET_CYCLE_BIT_SHIFT))

/**
 * NORMALIZE_EVENT_POINTER() -
 *
 * This macro will normalize the completion queue event entry so its value can
 * be used as an index.
 */
#define NORMALIZE_EVENT_POINTER(x) \
	(\
		((x) & SMU_COMPLETION_QUEUE_GET_EVENT_POINTER_MASK) \
		>> SMU_COMPLETION_QUEUE_GET_EVENT_POINTER_SHIFT	\
	)

/**
 * INCREMENT_COMPLETION_QUEUE_GET() -
 *
 * This macro will increment the controllers completion queue index value and
 * possibly toggle the cycle bit if the completion queue index wraps back to 0.
 */
#define INCREMENT_COMPLETION_QUEUE_GET(controller, index, cycle) \
	INCREMENT_QUEUE_GET(\
		(index), \
		(cycle), \
		(controller)->completion_queue_entries,	\
		SMU_CQGR_CYCLE_BIT \
		)

/**
 * INCREMENT_EVENT_QUEUE_GET() -
 *
 * This macro will increment the controllers event queue index value and
 * possibly toggle the event cycle bit if the event queue index wraps back to 0.
 */
#define INCREMENT_EVENT_QUEUE_GET(controller, index, cycle) \
	INCREMENT_QUEUE_GET(\
		(index), \
		(cycle), \
		(controller)->completion_event_entries,	\
		SMU_CQGR_EVENT_CYCLE_BIT \
		)

struct sci_base_memory_descriptor_list *
sci_controller_get_memory_descriptor_list_handle(struct scic_sds_controller *scic)
{
       return &scic->parent.mdl;
}

/*
 * ****************************************************************************-
 * * SCIC SDS Controller Initialization Methods
 * ****************************************************************************- */

/**
 * This timer is used to start another phy after we have given up on the
 *    previous phy to transition to the ready state.
 *
 *
 */
static void scic_sds_controller_phy_startup_timeout_handler(
	void *controller)
{
	enum sci_status status;
	struct scic_sds_controller *this_controller;

	this_controller = (struct scic_sds_controller *)controller;

	this_controller->phy_startup_timer_pending = false;

	status = SCI_FAILURE;

	while (status != SCI_SUCCESS) {
		status = scic_sds_controller_start_next_phy(this_controller);
	}
}

/**
 *
 *
 * This method initializes the phy startup operations for controller start.
 */
void scic_sds_controller_initialize_phy_startup(
	struct scic_sds_controller *this_controller)
{
	this_controller->phy_startup_timer = isci_event_timer_create(
		this_controller,
		scic_sds_controller_phy_startup_timeout_handler,
		this_controller
		);

	this_controller->next_phy_to_start = 0;
	this_controller->phy_startup_timer_pending = false;
}

/**
 *
 *
 * This method initializes the power control operations for the controller
 * object.
 */
void scic_sds_controller_initialize_power_control(
	struct scic_sds_controller *this_controller)
{
	this_controller->power_control.timer = isci_event_timer_create(
		this_controller,
		scic_sds_controller_power_control_timer_handler,
		this_controller
		);

	memset(
		this_controller->power_control.requesters,
		0,
		sizeof(this_controller->power_control.requesters)
		);

	this_controller->power_control.phys_waiting = 0;
}

/* --------------------------------------------------------------------------- */

#define SCU_REMOTE_NODE_CONTEXT_ALIGNMENT       (32)
#define SCU_TASK_CONTEXT_ALIGNMENT              (256)
#define SCU_UNSOLICITED_FRAME_ADDRESS_ALIGNMENT (64)
#define SCU_UNSOLICITED_FRAME_BUFFER_ALIGNMENT  (1024)
#define SCU_UNSOLICITED_FRAME_HEADER_ALIGNMENT  (64)

/* --------------------------------------------------------------------------- */

/**
 * This method builds the memory descriptor table for this controller.
 * @this_controller: This parameter specifies the controller object for which
 *    to build the memory table.
 *
 */
static void scic_sds_controller_build_memory_descriptor_table(
	struct scic_sds_controller *this_controller)
{
	sci_base_mde_construct(
		&this_controller->memory_descriptors[SCU_MDE_COMPLETION_QUEUE],
		SCU_COMPLETION_RAM_ALIGNMENT,
		(sizeof(u32) * this_controller->completion_queue_entries),
		(SCI_MDE_ATTRIBUTE_CACHEABLE | SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS)
		);

	sci_base_mde_construct(
		&this_controller->memory_descriptors[SCU_MDE_REMOTE_NODE_CONTEXT],
		SCU_REMOTE_NODE_CONTEXT_ALIGNMENT,
		this_controller->remote_node_entries * sizeof(union scu_remote_node_context),
		SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS
		);

	sci_base_mde_construct(
		&this_controller->memory_descriptors[SCU_MDE_TASK_CONTEXT],
		SCU_TASK_CONTEXT_ALIGNMENT,
		this_controller->task_context_entries * sizeof(struct scu_task_context),
		SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS
		);

	/*
	 * The UF buffer address table size must be programmed to a power
	 * of 2.  Find the first power of 2 that is equal to or greater then
	 * the number of unsolicited frame buffers to be utilized. */
	scic_sds_unsolicited_frame_control_set_address_table_count(
		&this_controller->uf_control
		);

	sci_base_mde_construct(
		&this_controller->memory_descriptors[SCU_MDE_UF_BUFFER],
		SCU_UNSOLICITED_FRAME_BUFFER_ALIGNMENT,
		scic_sds_unsolicited_frame_control_get_mde_size(this_controller->uf_control),
		SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS
		);
}

/**
 * This method validates the driver supplied memory descriptor table.
 * @this_controller:
 *
 * enum sci_status
 */
enum sci_status scic_sds_controller_validate_memory_descriptor_table(
	struct scic_sds_controller *this_controller)
{
	bool mde_list_valid;

	mde_list_valid = sci_base_mde_is_valid(
		&this_controller->memory_descriptors[SCU_MDE_COMPLETION_QUEUE],
		SCU_COMPLETION_RAM_ALIGNMENT,
		(sizeof(u32) * this_controller->completion_queue_entries),
		(SCI_MDE_ATTRIBUTE_CACHEABLE | SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS)
		);

	if (mde_list_valid == false)
		return SCI_FAILURE_UNSUPPORTED_INFORMATION_FIELD;

	mde_list_valid = sci_base_mde_is_valid(
		&this_controller->memory_descriptors[SCU_MDE_REMOTE_NODE_CONTEXT],
		SCU_REMOTE_NODE_CONTEXT_ALIGNMENT,
		this_controller->remote_node_entries * sizeof(union scu_remote_node_context),
		SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS
		);

	if (mde_list_valid == false)
		return SCI_FAILURE_UNSUPPORTED_INFORMATION_FIELD;

	mde_list_valid = sci_base_mde_is_valid(
		&this_controller->memory_descriptors[SCU_MDE_TASK_CONTEXT],
		SCU_TASK_CONTEXT_ALIGNMENT,
		this_controller->task_context_entries * sizeof(struct scu_task_context),
		SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS
		);

	if (mde_list_valid == false)
		return SCI_FAILURE_UNSUPPORTED_INFORMATION_FIELD;

	mde_list_valid = sci_base_mde_is_valid(
		&this_controller->memory_descriptors[SCU_MDE_UF_BUFFER],
		SCU_UNSOLICITED_FRAME_BUFFER_ALIGNMENT,
		scic_sds_unsolicited_frame_control_get_mde_size(this_controller->uf_control),
		SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS
		);

	if (mde_list_valid == false)
		return SCI_FAILURE_UNSUPPORTED_INFORMATION_FIELD;

	return SCI_SUCCESS;
}

/**
 * This method initializes the controller with the physical memory addresses
 *    that are used to communicate with the driver.
 * @this_controller:
 *
 */
void scic_sds_controller_ram_initialization(
	struct scic_sds_controller *this_controller)
{
	struct sci_physical_memory_descriptor *mde;

	/*
	 * The completion queue is actually placed in cacheable memory
	 * Therefore it no longer comes out of memory in the MDL. */
	mde = &this_controller->memory_descriptors[SCU_MDE_COMPLETION_QUEUE];
	this_controller->completion_queue = (u32 *)mde->virtual_address;
	SMU_CQBAR_WRITE(this_controller, mde->physical_address);

	/*
	 * Program the location of the Remote Node Context table
	 * into the SCU. */
	mde = &this_controller->memory_descriptors[SCU_MDE_REMOTE_NODE_CONTEXT];
	this_controller->remote_node_context_table = (union scu_remote_node_context *)
						     mde->virtual_address;
	SMU_RNCBAR_WRITE(this_controller, mde->physical_address);

	/* Program the location of the Task Context table into the SCU. */
	mde = &this_controller->memory_descriptors[SCU_MDE_TASK_CONTEXT];
	this_controller->task_context_table = (struct scu_task_context *)
					      mde->virtual_address;
	SMU_HTTBAR_WRITE(this_controller, mde->physical_address);

	mde = &this_controller->memory_descriptors[SCU_MDE_UF_BUFFER];
	scic_sds_unsolicited_frame_control_construct(
		&this_controller->uf_control, mde, this_controller
		);

	/*
	 * Inform the silicon as to the location of the UF headers and
	 * address table. */
	SCU_UFHBAR_WRITE(
		this_controller,
		this_controller->uf_control.headers.physical_address);
	SCU_PUFATHAR_WRITE(
		this_controller,
		this_controller->uf_control.address_table.physical_address);
}

/**
 * This method initializes the task context data for the controller.
 * @this_controller:
 *
 */
void scic_sds_controller_assign_task_entries(
	struct scic_sds_controller *this_controller)
{
	u32 task_assignment;

	/*
	 * Assign all the TCs to function 0
	 * TODO: Do we actually need to read this register to write it back? */
	task_assignment = SMU_TCA_READ(this_controller, 0);

	task_assignment =
		(
			task_assignment
			| (SMU_TCA_GEN_VAL(STARTING, 0))
			| (SMU_TCA_GEN_VAL(ENDING,  this_controller->task_context_entries - 1))
			| (SMU_TCA_GEN_BIT(RANGE_CHECK_ENABLE))
		);

	SMU_TCA_WRITE(this_controller, 0, task_assignment);
}

/**
 * This method initializes the hardware completion queue.
 *
 *
 */
void scic_sds_controller_initialize_completion_queue(
	struct scic_sds_controller *this_controller)
{
	u32 index;
	u32 completion_queue_control_value;
	u32 completion_queue_get_value;
	u32 completion_queue_put_value;

	this_controller->completion_queue_get = 0;

	completion_queue_control_value = (
		SMU_CQC_QUEUE_LIMIT_SET(this_controller->completion_queue_entries - 1)
		| SMU_CQC_EVENT_LIMIT_SET(this_controller->completion_event_entries - 1)
		);

	SMU_CQC_WRITE(this_controller, completion_queue_control_value);

	/* Set the completion queue get pointer and enable the queue */
	completion_queue_get_value = (
		(SMU_CQGR_GEN_VAL(POINTER, 0))
		| (SMU_CQGR_GEN_VAL(EVENT_POINTER, 0))
		| (SMU_CQGR_GEN_BIT(ENABLE))
		| (SMU_CQGR_GEN_BIT(EVENT_ENABLE))
		);

	SMU_CQGR_WRITE(this_controller, completion_queue_get_value);

	/* Set the completion queue put pointer */
	completion_queue_put_value = (
		(SMU_CQPR_GEN_VAL(POINTER, 0))
		| (SMU_CQPR_GEN_VAL(EVENT_POINTER, 0))
		);

	SMU_CQPR_WRITE(this_controller, completion_queue_put_value);

	/* Initialize the cycle bit of the completion queue entries */
	for (index = 0; index < this_controller->completion_queue_entries; index++) {
		/*
		 * If get.cycle_bit != completion_queue.cycle_bit
		 * its not a valid completion queue entry
		 * so at system start all entries are invalid */
		this_controller->completion_queue[index] = 0x80000000;
	}
}

/**
 * This method initializes the hardware unsolicited frame queue.
 *
 *
 */
void scic_sds_controller_initialize_unsolicited_frame_queue(
	struct scic_sds_controller *this_controller)
{
	u32 frame_queue_control_value;
	u32 frame_queue_get_value;
	u32 frame_queue_put_value;

	/* Write the queue size */
	frame_queue_control_value =
		SCU_UFQC_GEN_VAL(QUEUE_SIZE, this_controller->uf_control.address_table.count);

	SCU_UFQC_WRITE(this_controller, frame_queue_control_value);

	/* Setup the get pointer for the unsolicited frame queue */
	frame_queue_get_value = (
		SCU_UFQGP_GEN_VAL(POINTER, 0)
		|  SCU_UFQGP_GEN_BIT(ENABLE_BIT)
		);

	SCU_UFQGP_WRITE(this_controller, frame_queue_get_value);

	/* Setup the put pointer for the unsolicited frame queue */
	frame_queue_put_value = SCU_UFQPP_GEN_VAL(POINTER, 0);

	SCU_UFQPP_WRITE(this_controller, frame_queue_put_value);
}

/**
 * This method enables the hardware port task scheduler.
 *
 *
 */
void scic_sds_controller_enable_port_task_scheduler(
	struct scic_sds_controller *this_controller)
{
	u32 port_task_scheduler_value;

	port_task_scheduler_value = SCU_PTSGCR_READ(this_controller);

	port_task_scheduler_value |=
		(SCU_PTSGCR_GEN_BIT(ETM_ENABLE) | SCU_PTSGCR_GEN_BIT(PTSG_ENABLE));

	SCU_PTSGCR_WRITE(this_controller, port_task_scheduler_value);
}

/* --------------------------------------------------------------------------- */

/**
 *
 *
 * This macro is used to delay between writes to the AFE registers during AFE
 * initialization.
 */
#define AFE_REGISTER_WRITE_DELAY 10

/* Initialize the AFE for this phy index. We need to read the AFE setup from
 * the OEM parameters none
 */
void scic_sds_controller_afe_initialization(struct scic_sds_controller *scic)
{
	u32 afe_status;
	u32 phy_id;

	/* Clear DFX Status registers */
	scu_afe_register_write(scic, afe_dfx_master_control0, 0x0081000f);
	udelay(AFE_REGISTER_WRITE_DELAY);

	/* Configure bias currents to normal */
	if (is_a0())
		scu_afe_register_write(scic, afe_bias_control, 0x00005500);
	else
		scu_afe_register_write(scic, afe_bias_control, 0x00005A00);

	udelay(AFE_REGISTER_WRITE_DELAY);

	/* Enable PLL */
	if (is_b0())
		scu_afe_register_write(scic, afe_pll_control0, 0x80040A08);
	else
		scu_afe_register_write(scic, afe_pll_control0, 0x80040908);

	udelay(AFE_REGISTER_WRITE_DELAY);

	/* Wait for the PLL to lock */
	do {
		afe_status = scu_afe_register_read(
			scic, afe_common_block_status);
		udelay(AFE_REGISTER_WRITE_DELAY);
	} while ((afe_status & 0x00001000) == 0);

	if (is_b0()) {
		/* Shorten SAS SNW lock time (RxLock timer value from 76 us to 50 us) */
		scu_afe_register_write(scic, afe_pmsn_master_control0, 0x7bcc96ad);
		udelay(AFE_REGISTER_WRITE_DELAY);
	}

	for (phy_id = 0; phy_id < SCI_MAX_PHYS; phy_id++) {
		if (is_b0()) {
			 /* Configure transmitter SSC parameters */
			scu_afe_txreg_write(scic, phy_id, afe_tx_ssc_control, 0x00030000);
			udelay(AFE_REGISTER_WRITE_DELAY);
		} else {
			/*
			 * All defaults, except the Receive Word Alignament/Comma Detect
			 * Enable....(0xe800) */
			scu_afe_txreg_write(scic, phy_id, afe_xcvr_control0, 0x00004512);
			udelay(AFE_REGISTER_WRITE_DELAY);

			scu_afe_txreg_write(scic, phy_id, afe_xcvr_control1, 0x0050100F);
			udelay(AFE_REGISTER_WRITE_DELAY);
		}

		/*
		 * Power up TX and RX out from power down (PWRDNTX and PWRDNRX)
		 * & increase TX int & ext bias 20%....(0xe85c) */
		if (is_a0())
			scu_afe_txreg_write(scic, phy_id, afe_channel_control, 0x000003D4);
		else if (is_a2())
			scu_afe_txreg_write(scic, phy_id, afe_channel_control, 0x000003F0);
		else {
			 /* Power down TX and RX (PWRDNTX and PWRDNRX) */
			scu_afe_txreg_write(scic, phy_id, afe_channel_control, 0x000003d7);
			udelay(AFE_REGISTER_WRITE_DELAY);

			/*
			 * Power up TX and RX out from power down (PWRDNTX and PWRDNRX)
			 * & increase TX int & ext bias 20%....(0xe85c) */
			scu_afe_txreg_write(scic, phy_id, afe_channel_control, 0x000003d4);
		}
		udelay(AFE_REGISTER_WRITE_DELAY);

		if (is_a0() || is_a2()) {
			/* Enable TX equalization (0xe824) */
			scu_afe_txreg_write(scic, phy_id, afe_tx_control, 0x00040000);
			udelay(AFE_REGISTER_WRITE_DELAY);
		}

		/*
		 * RDPI=0x0(RX Power On), RXOOBDETPDNC=0x0, TPD=0x0(TX Power On),
		 * RDD=0x0(RX Detect Enabled) ....(0xe800) */
		scu_afe_txreg_write(scic, phy_id, afe_xcvr_control0, 0x00004100);
		udelay(AFE_REGISTER_WRITE_DELAY);

		/* Leave DFE/FFE on */
		if (is_a0())
			scu_afe_txreg_write(scic, phy_id, afe_rx_ssc_control0, 0x3F09983F);
		else if (is_a2())
			scu_afe_txreg_write(scic, phy_id, afe_rx_ssc_control0, 0x3F11103F);
		else {
			scu_afe_txreg_write(scic, phy_id, afe_rx_ssc_control0, 0x3F11103F);
			udelay(AFE_REGISTER_WRITE_DELAY);
			/* Enable TX equalization (0xe824) */
			scu_afe_txreg_write(scic, phy_id, afe_tx_control, 0x00040000);
		}
		udelay(AFE_REGISTER_WRITE_DELAY);

		scu_afe_txreg_write(scic, phy_id, afe_tx_amp_control0, 0x000E7C03);
		udelay(AFE_REGISTER_WRITE_DELAY);

		scu_afe_txreg_write(scic, phy_id, afe_tx_amp_control1, 0x000E7C03);
		udelay(AFE_REGISTER_WRITE_DELAY);

		scu_afe_txreg_write(scic, phy_id, afe_tx_amp_control2, 0x000E7C03);
		udelay(AFE_REGISTER_WRITE_DELAY);

		scu_afe_txreg_write(scic, phy_id, afe_tx_amp_control3, 0x000E7C03);
		udelay(AFE_REGISTER_WRITE_DELAY);
	}

	/* Transfer control to the PEs */
	scu_afe_register_write(scic, afe_dfx_master_control0, 0x00010f00);
	udelay(AFE_REGISTER_WRITE_DELAY);
}

/*
 * ****************************************************************************-
 * * SCIC SDS Controller Internal Start/Stop Routines
 * ****************************************************************************- */


/**
 * This method will attempt to transition into the ready state for the
 *    controller and indicate that the controller start operation has completed
 *    if all criteria are met.
 * @this_controller: This parameter indicates the controller object for which
 *    to transition to ready.
 * @status: This parameter indicates the status value to be pass into the call
 *    to scic_cb_controller_start_complete().
 *
 * none.
 */
static void scic_sds_controller_transition_to_ready(
	struct scic_sds_controller *this_controller,
	enum sci_status status)
{
	if (this_controller->parent.state_machine.current_state_id
	    == SCI_BASE_CONTROLLER_STATE_STARTING) {
		/*
		 * We move into the ready state, because some of the phys/ports
		 * may be up and operational. */
		sci_base_state_machine_change_state(
			scic_sds_controller_get_base_state_machine(this_controller),
			SCI_BASE_CONTROLLER_STATE_READY
			);

		isci_event_controller_start_complete(this_controller, status);
	}
}

/**
 * This method is the general timeout handler for the controller. It will take
 *    the correct timetout action based on the current controller state
 */
void scic_sds_controller_timeout_handler(
	struct scic_sds_controller *scic)
{
	enum sci_base_controller_states current_state;

	current_state = sci_base_state_machine_get_state(
		scic_sds_controller_get_base_state_machine(scic));

	if (current_state == SCI_BASE_CONTROLLER_STATE_STARTING) {
		scic_sds_controller_transition_to_ready(
			scic, SCI_FAILURE_TIMEOUT);
	} else if (current_state == SCI_BASE_CONTROLLER_STATE_STOPPING) {
		sci_base_state_machine_change_state(
			scic_sds_controller_get_base_state_machine(scic),
			SCI_BASE_CONTROLLER_STATE_FAILED);
		isci_event_controller_stop_complete(scic, SCI_FAILURE_TIMEOUT);
	} else	/* / @todo Now what do we want to do in this case? */
		dev_err(scic_to_dev(scic),
			"%s: Controller timer fired when controller was not "
			"in a state being timed.\n",
			__func__);
}

/**
 * scic_sds_controller_get_port_configuration_mode
 * @this_controller: This is the controller to use to determine if we are using
 *    manual or automatic port configuration.
 *
 * SCIC_PORT_CONFIGURATION_MODE
 */
enum SCIC_PORT_CONFIGURATION_MODE scic_sds_controller_get_port_configuration_mode(
	struct scic_sds_controller *this_controller)
{
	u32 index;
	enum SCIC_PORT_CONFIGURATION_MODE mode;

	mode = SCIC_PORT_AUTOMATIC_CONFIGURATION_MODE;

	for (index = 0; index < SCI_MAX_PORTS; index++) {
		if (this_controller->oem_parameters.sds1.ports[index].phy_mask != 0) {
			mode = SCIC_PORT_MANUAL_CONFIGURATION_MODE;
			break;
		}
	}

	return mode;
}

enum sci_status scic_sds_controller_stop_ports(struct scic_sds_controller *scic)
{
	u32 index;
	enum sci_status port_status;
	enum sci_status status = SCI_SUCCESS;

	for (index = 0; index < scic->logical_port_entries; index++) {
		port_status = scic_port_stop(&scic->port_table[index]);

		if ((port_status != SCI_SUCCESS) &&
		    (port_status != SCI_FAILURE_INVALID_STATE)) {
			status = SCI_FAILURE;

			dev_warn(scic_to_dev(scic),
				 "%s: Controller stop operation failed to "
				 "stop port %d because of status %d.\n",
				 __func__,
				 scic->port_table[index].logical_port_index,
				 port_status);
		}
	}

	return status;
}

/**
 *
 *
 *
 */
static void scic_sds_controller_phy_timer_start(
	struct scic_sds_controller *this_controller)
{
	isci_event_timer_start(
		this_controller,
		this_controller->phy_startup_timer,
		SCIC_SDS_CONTROLLER_PHY_START_TIMEOUT
		);

	this_controller->phy_startup_timer_pending = true;
}

/**
 *
 *
 *
 */
void scic_sds_controller_phy_timer_stop(
	struct scic_sds_controller *this_controller)
{
	isci_event_timer_stop(
		this_controller,
		this_controller->phy_startup_timer
		);

	this_controller->phy_startup_timer_pending = false;
}

/**
 * This method is called internally by the controller object to start the next
 *    phy on the controller.  If all the phys have been starte, then this
 *    method will attempt to transition the controller to the READY state and
 *    inform the user (scic_cb_controller_start_complete()).
 * @this_controller: This parameter specifies the controller object for which
 *    to start the next phy.
 *
 * enum sci_status
 */
enum sci_status scic_sds_controller_start_next_phy(
	struct scic_sds_controller *this_controller)
{
	enum sci_status status;

	status = SCI_SUCCESS;

	if (this_controller->phy_startup_timer_pending == false) {
		if (this_controller->next_phy_to_start == SCI_MAX_PHYS) {
			bool is_controller_start_complete = true;
			struct scic_sds_phy *the_phy;
			u8 index;

			for (index = 0; index < SCI_MAX_PHYS; index++) {
				the_phy = &this_controller->phy_table[index];

				if (scic_sds_phy_get_port(the_phy) != NULL) {
					/**
					 * The controller start operation is complete if and only
					 * if:
					 * - all links have been given an opportunity to start
					 * - have no indication of a connected device
					 * - have an indication of a connected device and it has
					 *   finished the link training process.
					 */
					if (
						(
							(the_phy->is_in_link_training == false)
							&& (the_phy->parent.state_machine.current_state_id
							    == SCI_BASE_PHY_STATE_INITIAL)
						)
						|| (
							(the_phy->is_in_link_training == false)
							&& (the_phy->parent.state_machine.current_state_id
							    == SCI_BASE_PHY_STATE_STOPPED)
							)
						|| (
							(the_phy->is_in_link_training == true)
							&& (the_phy->parent.state_machine.current_state_id
							    == SCI_BASE_PHY_STATE_STARTING)
							)
						) {
						is_controller_start_complete = false;
						break;
					}
				}
			}

			/*
			 * The controller has successfully finished the start process.
			 * Inform the SCI Core user and transition to the READY state. */
			if (is_controller_start_complete == true) {
				scic_sds_controller_transition_to_ready(
					this_controller, SCI_SUCCESS
					);
				scic_sds_controller_phy_timer_stop(this_controller);
			}
		} else {
			struct scic_sds_phy *the_phy;

			the_phy = &this_controller->phy_table[this_controller->next_phy_to_start];

			if (
				scic_sds_controller_get_port_configuration_mode(this_controller)
				== SCIC_PORT_MANUAL_CONFIGURATION_MODE
				) {
				if (scic_sds_phy_get_port(the_phy) == NULL) {
					this_controller->next_phy_to_start++;

					/*
					 * Caution recursion ahead be forwarned
					 *
					 * The PHY was never added to a PORT in MPC mode so start the next phy in sequence
					 * This phy will never go link up and will not draw power the OEM parameters either
					 * configured the phy incorrectly for the PORT or it was never assigned to a PORT */
					return scic_sds_controller_start_next_phy(this_controller);
				}
			}

			status = scic_sds_phy_start(the_phy);

			if (status == SCI_SUCCESS) {
				scic_sds_controller_phy_timer_start(this_controller);
			} else {
				dev_warn(scic_to_dev(this_controller),
					 "%s: Controller stop operation failed "
					 "to stop phy %d because of status "
					 "%d.\n",
					 __func__,
					 this_controller->phy_table[this_controller->next_phy_to_start].phy_index,
					 status);
			}

			this_controller->next_phy_to_start++;
		}
	}

	return status;
}

/**
 *
 * @this_controller:
 *
 * enum sci_status
 */
enum sci_status scic_sds_controller_stop_phys(
	struct scic_sds_controller *this_controller)
{
	u32 index;
	enum sci_status status;
	enum sci_status phy_status;

	status = SCI_SUCCESS;

	for (index = 0; index < SCI_MAX_PHYS; index++) {
		phy_status = scic_sds_phy_stop(&this_controller->phy_table[index]);

		if (
			(phy_status != SCI_SUCCESS)
			&& (phy_status != SCI_FAILURE_INVALID_STATE)
			) {
			status = SCI_FAILURE;

			dev_warn(scic_to_dev(this_controller),
				 "%s: Controller stop operation failed to stop "
				 "phy %d because of status %d.\n",
				 __func__,
				 this_controller->phy_table[index].phy_index, phy_status);
		}
	}

	return status;
}

/**
 *
 * @this_controller:
 *
 * enum sci_status
 */
enum sci_status scic_sds_controller_stop_devices(
	struct scic_sds_controller *this_controller)
{
	u32 index;
	enum sci_status status;
	enum sci_status device_status;

	status = SCI_SUCCESS;

	for (index = 0; index < this_controller->remote_node_entries; index++) {
		if (this_controller->device_table[index] != NULL) {
			/* / @todo What timeout value do we want to provide to this request? */
			device_status = scic_remote_device_stop(this_controller->device_table[index], 0);

			if ((device_status != SCI_SUCCESS) &&
			    (device_status != SCI_FAILURE_INVALID_STATE)) {
				dev_warn(scic_to_dev(this_controller),
					 "%s: Controller stop operation failed "
					 "to stop device 0x%p because of "
					 "status %d.\n",
					 __func__,
					 this_controller->device_table[index], device_status);
			}
		}
	}

	return status;
}

/*
 * ****************************************************************************-
 * * SCIC SDS Controller Power Control (Staggered Spinup)
 * ****************************************************************************- */

/**
 *
 *
 * This method starts the power control timer for this controller object.
 */
static void scic_sds_controller_power_control_timer_start(
	struct scic_sds_controller *this_controller)
{
	isci_event_timer_start(
		this_controller, this_controller->power_control.timer,
		SCIC_SDS_CONTROLLER_POWER_CONTROL_INTERVAL
		);

	this_controller->power_control.timer_started = true;
}

/**
 *
 *
 *
 */
static void scic_sds_controller_power_control_timer_handler(
	void *controller)
{
	struct scic_sds_controller *this_controller;

	this_controller = (struct scic_sds_controller *)controller;

	if (this_controller->power_control.phys_waiting == 0) {
		this_controller->power_control.timer_started = false;
	} else {
		struct scic_sds_phy *the_phy = NULL;
		u8 i;

		for (i = 0;
		     (i < SCI_MAX_PHYS)
		     && (this_controller->power_control.phys_waiting != 0);
		     i++) {
			if (this_controller->power_control.requesters[i] != NULL) {
				the_phy = this_controller->power_control.requesters[i];
				this_controller->power_control.requesters[i] = NULL;
				this_controller->power_control.phys_waiting--;
				break;
			}
		}

		/*
		 * It doesn't matter if the power list is empty, we need to start the
		 * timer in case another phy becomes ready. */
		scic_sds_controller_power_control_timer_start(this_controller);

		scic_sds_phy_consume_power_handler(the_phy);
	}
}

/**
 * This method inserts the phy in the stagger spinup control queue.
 * @this_controller:
 *
 *
 */
void scic_sds_controller_power_control_queue_insert(
	struct scic_sds_controller *this_controller,
	struct scic_sds_phy *the_phy)
{
	BUG_ON(the_phy == NULL);

	if (
		(this_controller->power_control.timer_started)
		&& (this_controller->power_control.requesters[the_phy->phy_index] == NULL)
		) {
		this_controller->power_control.requesters[the_phy->phy_index] = the_phy;
		this_controller->power_control.phys_waiting++;
	} else {
		scic_sds_controller_power_control_timer_start(this_controller);
		scic_sds_phy_consume_power_handler(the_phy);
	}
}

/**
 * This method removes the phy from the stagger spinup control queue.
 * @this_controller:
 *
 *
 */
void scic_sds_controller_power_control_queue_remove(
	struct scic_sds_controller *this_controller,
	struct scic_sds_phy *the_phy)
{
	BUG_ON(the_phy == NULL);

	if (this_controller->power_control.requesters[the_phy->phy_index] != NULL) {
		this_controller->power_control.phys_waiting--;
	}

	this_controller->power_control.requesters[the_phy->phy_index] = NULL;
}

/*
 * ****************************************************************************-
 * * SCIC SDS Controller Completion Routines
 * ****************************************************************************- */

/**
 * This method returns a true value if the completion queue has entries that
 *    can be processed
 * @this_controller:
 *
 * bool true if the completion queue has entries to process false if the
 * completion queue has no entries to process
 */
static bool scic_sds_controller_completion_queue_has_entries(
	struct scic_sds_controller *this_controller)
{
	u32 get_value = this_controller->completion_queue_get;
	u32 get_index = get_value & SMU_COMPLETION_QUEUE_GET_POINTER_MASK;

	if (
		NORMALIZE_GET_POINTER_CYCLE_BIT(get_value)
		== COMPLETION_QUEUE_CYCLE_BIT(this_controller->completion_queue[get_index])
		) {
		return true;
	}

	return false;
}

/* --------------------------------------------------------------------------- */

/**
 * This method processes a task completion notification.  This is called from
 *    within the controller completion handler.
 * @this_controller:
 * @completion_entry:
 *
 */
static void scic_sds_controller_task_completion(
	struct scic_sds_controller *this_controller,
	u32 completion_entry)
{
	u32 index;
	struct scic_sds_request *io_request;

	index = SCU_GET_COMPLETION_INDEX(completion_entry);
	io_request = this_controller->io_request_table[index];

	/* Make sure that we really want to process this IO request */
	if (
		(io_request != NULL)
		&& (io_request->io_tag != SCI_CONTROLLER_INVALID_IO_TAG)
		&& (
			scic_sds_io_tag_get_sequence(io_request->io_tag)
			== this_controller->io_request_sequence[index]
			)
		) {
		/* Yep this is a valid io request pass it along to the io request handler */
		scic_sds_io_request_tc_completion(io_request, completion_entry);
	}
}

/**
 * This method processes an SDMA completion event.  This is called from within
 *    the controller completion handler.
 * @this_controller:
 * @completion_entry:
 *
 */
static void scic_sds_controller_sdma_completion(
	struct scic_sds_controller *this_controller,
	u32 completion_entry)
{
	u32 index;
	struct scic_sds_request *io_request;
	struct scic_sds_remote_device *device;

	index = SCU_GET_COMPLETION_INDEX(completion_entry);

	switch (scu_get_command_request_type(completion_entry)) {
	case SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_TC:
	case SCU_CONTEXT_COMMAND_REQUEST_TYPE_DUMP_TC:
		io_request = this_controller->io_request_table[index];
		dev_warn(scic_to_dev(this_controller),
			 "%s: SCIC SDS Completion type SDMA %x for io request "
			 "%p\n",
			 __func__,
			 completion_entry,
			 io_request);
		/* @todo For a post TC operation we need to fail the IO
		 * request
		 */
		break;

	case SCU_CONTEXT_COMMAND_REQUEST_TYPE_DUMP_RNC:
	case SCU_CONTEXT_COMMAND_REQUEST_TYPE_OTHER_RNC:
	case SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_RNC:
		device = this_controller->device_table[index];
		dev_warn(scic_to_dev(this_controller),
			 "%s: SCIC SDS Completion type SDMA %x for remote "
			 "device %p\n",
			 __func__,
			 completion_entry,
			 device);
		/* @todo For a port RNC operation we need to fail the
		 * device
		 */
		break;

	default:
		dev_warn(scic_to_dev(this_controller),
			 "%s: SCIC SDS Completion unknown SDMA completion "
			 "type %x\n",
			 __func__,
			 completion_entry);
		break;

	}
}

/**
 *
 * @this_controller:
 * @completion_entry:
 *
 * This method processes an unsolicited frame message.  This is called from
 * within the controller completion handler. none
 */
static void scic_sds_controller_unsolicited_frame(
	struct scic_sds_controller *this_controller,
	u32 completion_entry)
{
	u32 index;
	u32 frame_index;

	struct scu_unsolicited_frame_header *frame_header;
	struct scic_sds_phy *phy;
	struct scic_sds_remote_device *device;

	enum sci_status result = SCI_FAILURE;

	frame_index = SCU_GET_FRAME_INDEX(completion_entry);

	frame_header
		= this_controller->uf_control.buffers.array[frame_index].header;
	this_controller->uf_control.buffers.array[frame_index].state
		= UNSOLICITED_FRAME_IN_USE;

	if (SCU_GET_FRAME_ERROR(completion_entry)) {
		/*
		 * / @todo If the IAF frame or SIGNATURE FIS frame has an error will
		 * /       this cause a problem? We expect the phy initialization will
		 * /       fail if there is an error in the frame. */
		scic_sds_controller_release_frame(this_controller, frame_index);
		return;
	}

	if (frame_header->is_address_frame) {
		index = SCU_GET_PROTOCOL_ENGINE_INDEX(completion_entry);
		phy = &this_controller->phy_table[index];
		if (phy != NULL) {
			result = scic_sds_phy_frame_handler(phy, frame_index);
		}
	} else {

		index = SCU_GET_COMPLETION_INDEX(completion_entry);

		if (index == SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX) {
			/*
			 * This is a signature fis or a frame from a direct attached SATA
			 * device that has not yet been created.  In either case forwared
			 * the frame to the PE and let it take care of the frame data. */
			index = SCU_GET_PROTOCOL_ENGINE_INDEX(completion_entry);
			phy = &this_controller->phy_table[index];
			result = scic_sds_phy_frame_handler(phy, frame_index);
		} else {
			if (index < this_controller->remote_node_entries)
				device = this_controller->device_table[index];
			else
				device = NULL;

			if (device != NULL)
				result = scic_sds_remote_device_frame_handler(device, frame_index);
			else
				scic_sds_controller_release_frame(this_controller, frame_index);
		}
	}

	if (result != SCI_SUCCESS) {
		/*
		 * / @todo Is there any reason to report some additional error message
		 * /       when we get this failure notifiction? */
	}
}

/**
 * This method processes an event completion entry.  This is called from within
 *    the controller completion handler.
 * @this_controller:
 * @completion_entry:
 *
 */
static void scic_sds_controller_event_completion(
	struct scic_sds_controller *this_controller,
	u32 completion_entry)
{
	u32 index;
	struct scic_sds_request *io_request;
	struct scic_sds_remote_device *device;
	struct scic_sds_phy *phy;

	index = SCU_GET_COMPLETION_INDEX(completion_entry);

	switch (scu_get_event_type(completion_entry)) {
	case SCU_EVENT_TYPE_SMU_COMMAND_ERROR:
		/* / @todo The driver did something wrong and we need to fix the condtion. */
		dev_err(scic_to_dev(this_controller),
			"%s: SCIC Controller 0x%p received SMU command error "
			"0x%x\n",
			__func__,
			this_controller,
			completion_entry);
		break;

	case SCU_EVENT_TYPE_SMU_PCQ_ERROR:
	case SCU_EVENT_TYPE_SMU_ERROR:
	case SCU_EVENT_TYPE_FATAL_MEMORY_ERROR:
		/*
		 * / @todo This is a hardware failure and its likely that we want to
		 * /       reset the controller. */
		dev_err(scic_to_dev(this_controller),
			"%s: SCIC Controller 0x%p received fatal controller "
			"event  0x%x\n",
			__func__,
			this_controller,
			completion_entry);
		break;

	case SCU_EVENT_TYPE_TRANSPORT_ERROR:
		io_request = this_controller->io_request_table[index];
		scic_sds_io_request_event_handler(io_request, completion_entry);
		break;

	case SCU_EVENT_TYPE_PTX_SCHEDULE_EVENT:
		switch (scu_get_event_specifier(completion_entry)) {
		case SCU_EVENT_SPECIFIC_SMP_RESPONSE_NO_PE:
		case SCU_EVENT_SPECIFIC_TASK_TIMEOUT:
			io_request = this_controller->io_request_table[index];
			if (io_request != NULL)
				scic_sds_io_request_event_handler(io_request, completion_entry);
			else
				dev_warn(scic_to_dev(this_controller),
					 "%s: SCIC Controller 0x%p received "
					 "event 0x%x for io request object "
					 "that doesnt exist.\n",
					 __func__,
					 this_controller,
					 completion_entry);

			break;

		case SCU_EVENT_SPECIFIC_IT_NEXUS_TIMEOUT:
			device = this_controller->device_table[index];
			if (device != NULL)
				scic_sds_remote_device_event_handler(device, completion_entry);
			else
				dev_warn(scic_to_dev(this_controller),
					 "%s: SCIC Controller 0x%p received "
					 "event 0x%x for remote device object "
					 "that doesnt exist.\n",
					 __func__,
					 this_controller,
					 completion_entry);

			break;
		}
		break;

	case SCU_EVENT_TYPE_BROADCAST_CHANGE:
	/*
	 * direct the broadcast change event to the phy first and then let
	 * the phy redirect the broadcast change to the port object */
	case SCU_EVENT_TYPE_ERR_CNT_EVENT:
	/*
	 * direct error counter event to the phy object since that is where
	 * we get the event notification.  This is a type 4 event. */
	case SCU_EVENT_TYPE_OSSP_EVENT:
		index = SCU_GET_PROTOCOL_ENGINE_INDEX(completion_entry);
		phy = &this_controller->phy_table[index];
		scic_sds_phy_event_handler(phy, completion_entry);
		break;

	case SCU_EVENT_TYPE_RNC_SUSPEND_TX:
	case SCU_EVENT_TYPE_RNC_SUSPEND_TX_RX:
	case SCU_EVENT_TYPE_RNC_OPS_MISC:
		if (index < this_controller->remote_node_entries) {
			device = this_controller->device_table[index];

			if (device != NULL)
				scic_sds_remote_device_event_handler(device, completion_entry);
		} else
			dev_err(scic_to_dev(this_controller),
				"%s: SCIC Controller 0x%p received event 0x%x "
				"for remote device object 0x%0x that doesnt "
				"exist.\n",
				__func__,
				this_controller,
				completion_entry,
				index);

		break;

	default:
		dev_warn(scic_to_dev(this_controller),
			 "%s: SCIC Controller received unknown event code %x\n",
			 __func__,
			 completion_entry);
		break;
	}
}

/**
 * This method is a private routine for processing the completion queue entries.
 * @this_controller:
 *
 */
static void scic_sds_controller_process_completions(
	struct scic_sds_controller *this_controller)
{
	u32 completion_count = 0;
	u32 completion_entry;
	u32 get_index;
	u32 get_cycle;
	u32 event_index;
	u32 event_cycle;

	dev_dbg(scic_to_dev(this_controller),
		"%s: completion queue begining get:0x%08x\n",
		__func__,
		this_controller->completion_queue_get);

	/* Get the component parts of the completion queue */
	get_index = NORMALIZE_GET_POINTER(this_controller->completion_queue_get);
	get_cycle = SMU_CQGR_CYCLE_BIT & this_controller->completion_queue_get;

	event_index = NORMALIZE_EVENT_POINTER(this_controller->completion_queue_get);
	event_cycle = SMU_CQGR_EVENT_CYCLE_BIT & this_controller->completion_queue_get;

	while (
		NORMALIZE_GET_POINTER_CYCLE_BIT(get_cycle)
		== COMPLETION_QUEUE_CYCLE_BIT(this_controller->completion_queue[get_index])
		) {
		completion_count++;

		completion_entry = this_controller->completion_queue[get_index];
		INCREMENT_COMPLETION_QUEUE_GET(this_controller, get_index, get_cycle);

		dev_dbg(scic_to_dev(this_controller),
			"%s: completion queue entry:0x%08x\n",
			__func__,
			completion_entry);

		switch (SCU_GET_COMPLETION_TYPE(completion_entry)) {
		case SCU_COMPLETION_TYPE_TASK:
			scic_sds_controller_task_completion(this_controller, completion_entry);
			break;

		case SCU_COMPLETION_TYPE_SDMA:
			scic_sds_controller_sdma_completion(this_controller, completion_entry);
			break;

		case SCU_COMPLETION_TYPE_UFI:
			scic_sds_controller_unsolicited_frame(this_controller, completion_entry);
			break;

		case SCU_COMPLETION_TYPE_EVENT:
			INCREMENT_EVENT_QUEUE_GET(this_controller, event_index, event_cycle);
			scic_sds_controller_event_completion(this_controller, completion_entry);
			break;

		case SCU_COMPLETION_TYPE_NOTIFY:
			/*
			 * Presently we do the same thing with a notify event that we do with the
			 * other event codes. */
			INCREMENT_EVENT_QUEUE_GET(this_controller, event_index, event_cycle);
			scic_sds_controller_event_completion(this_controller, completion_entry);
			break;

		default:
			dev_warn(scic_to_dev(this_controller),
				 "%s: SCIC Controller received unknown "
				 "completion type %x\n",
				 __func__,
				 completion_entry);
			break;
		}
	}

	/* Update the get register if we completed one or more entries */
	if (completion_count > 0) {
		this_controller->completion_queue_get =
			SMU_CQGR_GEN_BIT(ENABLE)
			| SMU_CQGR_GEN_BIT(EVENT_ENABLE)
			| event_cycle | SMU_CQGR_GEN_VAL(EVENT_POINTER, event_index)
			| get_cycle   | SMU_CQGR_GEN_VAL(POINTER, get_index);

		SMU_CQGR_WRITE(this_controller,
			       this_controller->completion_queue_get);
	}

	dev_dbg(scic_to_dev(this_controller),
		"%s: completion queue ending get:0x%08x\n",
		__func__,
		this_controller->completion_queue_get);

}

bool scic_sds_controller_isr(struct scic_sds_controller *scic)
{
	if (scic_sds_controller_completion_queue_has_entries(scic)) {
		return true;
	} else {
		/*
		 * we have a spurious interrupt it could be that we have already
		 * emptied the completion queue from a previous interrupt */
		SMU_ISR_WRITE(scic, SMU_ISR_COMPLETION);

		/*
		 * There is a race in the hardware that could cause us not to be notified
		 * of an interrupt completion if we do not take this step.  We will mask
		 * then unmask the interrupts so if there is another interrupt pending
		 * the clearing of the interrupt source we get the next interrupt message. */
		SMU_IMR_WRITE(scic, 0xFF000000);
		SMU_IMR_WRITE(scic, 0x00000000);
	}

	return false;
}

void scic_sds_controller_completion_handler(struct scic_sds_controller *scic)
{
	/* Empty out the completion queue */
	if (scic_sds_controller_completion_queue_has_entries(scic))
		scic_sds_controller_process_completions(scic);

	/* Clear the interrupt and enable all interrupts again */
	SMU_ISR_WRITE(scic, SMU_ISR_COMPLETION);
	/* Could we write the value of SMU_ISR_COMPLETION? */
	SMU_IMR_WRITE(scic, 0xFF000000);
	SMU_IMR_WRITE(scic, 0x00000000);
}

bool scic_sds_controller_error_isr(struct scic_sds_controller *scic)
{
	u32 interrupt_status;

	interrupt_status = SMU_ISR_READ(scic);

	interrupt_status &= (SMU_ISR_QUEUE_ERROR | SMU_ISR_QUEUE_SUSPEND);

	if (interrupt_status != 0) {
		/*
		 * There is an error interrupt pending so let it through and handle
		 * in the callback */
		return true;
	}

	/*
	 * There is a race in the hardware that could cause us not to be notified
	 * of an interrupt completion if we do not take this step.  We will mask
	 * then unmask the error interrupts so if there was another interrupt
	 * pending we will be notified.
	 * Could we write the value of (SMU_ISR_QUEUE_ERROR | SMU_ISR_QUEUE_SUSPEND)? */
	SMU_IMR_WRITE(scic, 0x000000FF);
	SMU_IMR_WRITE(scic, 0x00000000);

	return false;
}

void scic_sds_controller_error_handler(struct scic_sds_controller *scic)
{
	u32 interrupt_status;

	interrupt_status = SMU_ISR_READ(scic);

	if ((interrupt_status & SMU_ISR_QUEUE_SUSPEND) &&
	    scic_sds_controller_completion_queue_has_entries(scic)) {

		scic_sds_controller_process_completions(scic);
		SMU_ISR_WRITE(scic, SMU_ISR_QUEUE_SUSPEND);

	} else {
		dev_err(scic_to_dev(scic), "%s: status: %#x\n", __func__,
			interrupt_status);

		sci_base_state_machine_change_state(
			scic_sds_controller_get_base_state_machine(scic),
			SCI_BASE_CONTROLLER_STATE_FAILED);

		return;
	}

	/*
	 * If we dont process any completions I am not sure that we want to do this.
	 * We are in the middle of a hardware fault and should probably be reset. */
	SMU_IMR_WRITE(scic, 0x00000000);
}


u32 scic_sds_controller_get_object_size(void)
{
	return sizeof(struct scic_sds_controller);
}


void scic_sds_controller_link_up(
	struct scic_sds_controller *scic,
	struct scic_sds_port *sci_port,
	struct scic_sds_phy *sci_phy)
{
	scic_sds_controller_phy_handler_t link_up;
	u32 state;

	state = scic->parent.state_machine.current_state_id;
	link_up = scic_sds_controller_state_handler_table[state].link_up;

	if (link_up)
		link_up(scic, sci_port, sci_phy);
	else
		dev_warn(scic_to_dev(scic),
			"%s: SCIC Controller linkup event from phy %d in "
			"unexpected state %d\n",
			__func__,
			sci_phy->phy_index,
			sci_base_state_machine_get_state(
				scic_sds_controller_get_base_state_machine(
					scic)));
}


void scic_sds_controller_link_down(
	struct scic_sds_controller *scic,
	struct scic_sds_port *sci_port,
	struct scic_sds_phy *sci_phy)
{
	u32 state;
	scic_sds_controller_phy_handler_t link_down;

	state = scic->parent.state_machine.current_state_id;
	link_down = scic_sds_controller_state_handler_table[state].link_down;

	if (link_down)
		link_down(scic, sci_port, sci_phy);
	else
		dev_warn(scic_to_dev(scic),
			"%s: SCIC Controller linkdown event from phy %d in "
			"unexpected state %d\n",
			__func__,
			sci_phy->phy_index,
			sci_base_state_machine_get_state(
				scic_sds_controller_get_base_state_machine(
					scic)));
}

/**
 * This method will write to the SCU PCP register the request value. The method
 *    is used to suspend/resume ports, devices, and phys.
 * @this_controller:
 *
 *
 */
void scic_sds_controller_post_request(
	struct scic_sds_controller *this_controller,
	u32 request)
{
	dev_dbg(scic_to_dev(this_controller),
		"%s: SCIC Controller 0x%p post request 0x%08x\n",
		__func__,
		this_controller,
		request);

	SMU_PCP_WRITE(this_controller, request);
}

/**
 * This method will copy the soft copy of the task context into the physical
 *    memory accessible by the controller.
 * @this_controller: This parameter specifies the controller for which to copy
 *    the task context.
 * @this_request: This parameter specifies the request for which the task
 *    context is being copied.
 *
 * After this call is made the SCIC_SDS_IO_REQUEST object will always point to
 * the physical memory version of the task context. Thus, all subsequent
 * updates to the task context are performed in the TC table (i.e. DMAable
 * memory). none
 */
void scic_sds_controller_copy_task_context(
	struct scic_sds_controller *this_controller,
	struct scic_sds_request *this_request)
{
	struct scu_task_context *task_context_buffer;

	task_context_buffer = scic_sds_controller_get_task_context_buffer(
		this_controller, this_request->io_tag
		);

	memcpy(
		task_context_buffer,
		this_request->task_context_buffer,
		SCI_FIELD_OFFSET(struct scu_task_context, sgl_snapshot_ac)
		);

	/*
	 * Now that the soft copy of the TC has been copied into the TC
	 * table accessible by the silicon.  Thus, any further changes to
	 * the TC (e.g. TC termination) occur in the appropriate location. */
	this_request->task_context_buffer = task_context_buffer;
}

/**
 * This method returns the task context buffer for the given io tag.
 * @this_controller:
 * @io_tag:
 *
 * struct scu_task_context*
 */
struct scu_task_context *scic_sds_controller_get_task_context_buffer(
	struct scic_sds_controller *this_controller,
	u16 io_tag
	) {
	u16 task_index = scic_sds_io_tag_get_index(io_tag);

	if (task_index < this_controller->task_context_entries) {
		return &this_controller->task_context_table[task_index];
	}

	return NULL;
}

/**
 * This method returnst the sequence value from the io tag value
 * @this_controller:
 * @io_tag:
 *
 * u16
 */

/**
 * This method returns the IO request associated with the tag value
 * @this_controller:
 * @io_tag:
 *
 * SCIC_SDS_IO_REQUEST_T* NULL if there is no valid IO request at the tag value
 */
struct scic_sds_request *scic_sds_controller_get_io_request_from_tag(
	struct scic_sds_controller *this_controller,
	u16 io_tag
	) {
	u16 task_index;
	u16 task_sequence;

	task_index = scic_sds_io_tag_get_index(io_tag);

	if (task_index  < this_controller->task_context_entries) {
		if (this_controller->io_request_table[task_index] != NULL) {
			task_sequence = scic_sds_io_tag_get_sequence(io_tag);

			if (task_sequence == this_controller->io_request_sequence[task_index]) {
				return this_controller->io_request_table[task_index];
			}
		}
	}

	return NULL;
}

/**
 * This method allocates remote node index and the reserves the remote node
 *    context space for use. This method can fail if there are no more remote
 *    node index available.
 * @this_controller: This is the controller object which contains the set of
 *    free remote node ids
 * @the_devce: This is the device object which is requesting the a remote node
 *    id
 * @node_id: This is the remote node id that is assinged to the device if one
 *    is available
 *
 * enum sci_status SCI_FAILURE_OUT_OF_RESOURCES if there are no available remote
 * node index available.
 */
enum sci_status scic_sds_controller_allocate_remote_node_context(
	struct scic_sds_controller *this_controller,
	struct scic_sds_remote_device *the_device,
	u16 *node_id)
{
	u16 node_index;
	u32 remote_node_count = scic_sds_remote_device_node_count(the_device);

	node_index = scic_sds_remote_node_table_allocate_remote_node(
		&this_controller->available_remote_nodes, remote_node_count
		);

	if (node_index != SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX) {
		this_controller->device_table[node_index] = the_device;

		*node_id = node_index;

		return SCI_SUCCESS;
	}

	return SCI_FAILURE_INSUFFICIENT_RESOURCES;
}

/**
 * This method frees the remote node index back to the available pool.  Once
 *    this is done the remote node context buffer is no longer valid and can
 *    not be used.
 * @this_controller:
 * @the_device:
 * @node_id:
 *
 */
void scic_sds_controller_free_remote_node_context(
	struct scic_sds_controller *this_controller,
	struct scic_sds_remote_device *the_device,
	u16 node_id)
{
	u32 remote_node_count = scic_sds_remote_device_node_count(the_device);

	if (this_controller->device_table[node_id] == the_device) {
		this_controller->device_table[node_id] = NULL;

		scic_sds_remote_node_table_release_remote_node_index(
			&this_controller->available_remote_nodes, remote_node_count, node_id
			);
	}
}

/**
 * This method returns the union scu_remote_node_context for the specified remote
 *    node id.
 * @this_controller:
 * @node_id:
 *
 * union scu_remote_node_context*
 */
union scu_remote_node_context *scic_sds_controller_get_remote_node_context_buffer(
	struct scic_sds_controller *this_controller,
	u16 node_id
	) {
	if (
		(node_id < this_controller->remote_node_entries)
		&& (this_controller->device_table[node_id] != NULL)
		) {
		return &this_controller->remote_node_context_table[node_id];
	}

	return NULL;
}

/**
 *
 * @resposne_buffer: This is the buffer into which the D2H register FIS will be
 *    constructed.
 * @frame_header: This is the frame header returned by the hardware.
 * @frame_buffer: This is the frame buffer returned by the hardware.
 *
 * This method will combind the frame header and frame buffer to create a SATA
 * D2H register FIS none
 */
void scic_sds_controller_copy_sata_response(
	void *response_buffer,
	void *frame_header,
	void *frame_buffer)
{
	memcpy(
		response_buffer,
		frame_header,
		sizeof(u32)
		);

	memcpy(
		(char *)((char *)response_buffer + sizeof(u32)),
		frame_buffer,
		sizeof(struct sata_fis_reg_d2h) - sizeof(u32)
		);
}

/**
 * This method releases the frame once this is done the frame is available for
 *    re-use by the hardware.  The data contained in the frame header and frame
 *    buffer is no longer valid. The UF queue get pointer is only updated if UF
 *    control indicates this is appropriate.
 * @this_controller:
 * @frame_index:
 *
 */
void scic_sds_controller_release_frame(
	struct scic_sds_controller *this_controller,
	u32 frame_index)
{
	if (scic_sds_unsolicited_frame_control_release_frame(
		    &this_controller->uf_control, frame_index) == true)
		SCU_UFQGP_WRITE(this_controller, this_controller->uf_control.get);
}

/**
 * This method sets user parameters and OEM parameters to default values.
 *    Users can override these values utilizing the scic_user_parameters_set()
 *    and scic_oem_parameters_set() methods.
 * @controller: This parameter specifies the controller for which to set the
 *    configuration parameters to their default values.
 *
 */
static void scic_sds_controller_set_default_config_parameters(struct scic_sds_controller *scic)
{
	u16 index;

	/* Default to no SSC operation. */
	scic->oem_parameters.sds1.controller.do_enable_ssc = false;

	/* Initialize all of the port parameter information to narrow ports. */
	for (index = 0; index < SCI_MAX_PORTS; index++) {
		scic->oem_parameters.sds1.ports[index].phy_mask = 0;
	}

	/* Initialize all of the phy parameter information. */
	for (index = 0; index < SCI_MAX_PHYS; index++) {
		/*
		 * Default to 3G (i.e. Gen 2) for now.  User can override if
		 * they choose. */
		scic->user_parameters.sds1.phys[index].max_speed_generation = 2;

		/* the frequencies cannot be 0 */
		scic->user_parameters.sds1.phys[index].align_insertion_frequency = 0x7f;
		scic->user_parameters.sds1.phys[index].in_connection_align_insertion_frequency = 0xff;
		scic->user_parameters.sds1.phys[index].notify_enable_spin_up_insertion_frequency = 0x33;

		/*
		 * Previous Vitesse based expanders had a arbitration issue that
		 * is worked around by having the upper 32-bits of SAS address
		 * with a value greater then the Vitesse company identifier.
		 * Hence, usage of 0x5FCFFFFF. */
		scic->oem_parameters.sds1.phys[index].sas_address.low = 0x00000001;
		scic->oem_parameters.sds1.phys[index].sas_address.high = 0x5FCFFFFF;
	}

	scic->user_parameters.sds1.stp_inactivity_timeout = 5;
	scic->user_parameters.sds1.ssp_inactivity_timeout = 5;
	scic->user_parameters.sds1.stp_max_occupancy_timeout = 5;
	scic->user_parameters.sds1.ssp_max_occupancy_timeout = 20;
	scic->user_parameters.sds1.no_outbound_task_timeout = 20;
}


enum sci_status scic_controller_construct(struct scic_sds_controller *controller,
					  void __iomem *scu_base,
					  void __iomem *smu_base)
{
	u8 index;

	sci_base_controller_construct(
		&controller->parent,
		scic_sds_controller_state_table,
		controller->memory_descriptors,
		ARRAY_SIZE(controller->memory_descriptors),
		NULL
		);

	controller->scu_registers = scu_base;
	controller->smu_registers = smu_base;

	scic_sds_port_configuration_agent_construct(&controller->port_agent);

	/* Construct the ports for this controller */
	for (index = 0; index < SCI_MAX_PORTS; index++)
		scic_sds_port_construct(&controller->port_table[index],
					index, controller);
	scic_sds_port_construct(&controller->port_table[index],
				SCIC_SDS_DUMMY_PORT, controller);

	/* Construct the phys for this controller */
	for (index = 0; index < SCI_MAX_PHYS; index++) {
		/* Add all the PHYs to the dummy port */
		scic_sds_phy_construct(
			&controller->phy_table[index],
			&controller->port_table[SCI_MAX_PORTS],
			index
			);
	}

	controller->invalid_phy_mask = 0;

	/* Set the default maximum values */
	controller->completion_event_entries      = SCU_EVENT_COUNT;
	controller->completion_queue_entries      = SCU_COMPLETION_QUEUE_COUNT;
	controller->remote_node_entries           = SCI_MAX_REMOTE_DEVICES;
	controller->logical_port_entries          = SCI_MAX_PORTS;
	controller->task_context_entries          = SCU_IO_REQUEST_COUNT;
	controller->uf_control.buffers.count      = SCU_UNSOLICITED_FRAME_COUNT;
	controller->uf_control.address_table.count = SCU_UNSOLICITED_FRAME_COUNT;

	/* Initialize the User and OEM parameters to default values. */
	scic_sds_controller_set_default_config_parameters(controller);

	return SCI_SUCCESS;
}

/* --------------------------------------------------------------------------- */

enum sci_status scic_controller_initialize(
	struct scic_sds_controller *scic)
{
	enum sci_status status = SCI_FAILURE_INVALID_STATE;
	sci_base_controller_handler_t initialize;
	u32 state;

	state = scic->parent.state_machine.current_state_id;
	initialize = scic_sds_controller_state_handler_table[state].base.initialize;

	if (initialize)
		status = initialize(&scic->parent);
	else
		dev_warn(scic_to_dev(scic),
			 "%s: SCIC Controller initialize operation requested "
			 "in invalid state %d\n",
			 __func__,
			 sci_base_state_machine_get_state(
				 scic_sds_controller_get_base_state_machine(
					 scic)));

	return status;
}

/* --------------------------------------------------------------------------- */

u32 scic_controller_get_suggested_start_timeout(
	struct scic_sds_controller *sc)
{
	/* Validate the user supplied parameters. */
	if (sc == NULL)
		return 0;

	/*
	 * The suggested minimum timeout value for a controller start operation:
	 *
	 *     Signature FIS Timeout
	 *   + Phy Start Timeout
	 *   + Number of Phy Spin Up Intervals
	 *   ---------------------------------
	 *   Number of milliseconds for the controller start operation.
	 *
	 * NOTE: The number of phy spin up intervals will be equivalent
	 *       to the number of phys divided by the number phys allowed
	 *       per interval - 1 (once OEM parameters are supported).
	 *       Currently we assume only 1 phy per interval. */

	return SCIC_SDS_SIGNATURE_FIS_TIMEOUT
		+ SCIC_SDS_CONTROLLER_PHY_START_TIMEOUT
		+ ((SCI_MAX_PHYS - 1) * SCIC_SDS_CONTROLLER_POWER_CONTROL_INTERVAL);
}

/* --------------------------------------------------------------------------- */

enum sci_status scic_controller_start(
	struct scic_sds_controller *scic,
	u32 timeout)
{
	enum sci_status status = SCI_FAILURE_INVALID_STATE;
	sci_base_controller_timed_handler_t start;
	u32 state;

	state = scic->parent.state_machine.current_state_id;
	start = scic_sds_controller_state_handler_table[state].base.start;

	if (start)
		status = start(&scic->parent, timeout);
	else
		dev_warn(scic_to_dev(scic),
			 "%s: SCIC Controller start operation requested in "
			 "invalid state %d\n",
			 __func__,
			 sci_base_state_machine_get_state(
				 scic_sds_controller_get_base_state_machine(
					 scic)));

	return status;
}

/* --------------------------------------------------------------------------- */

enum sci_status scic_controller_stop(
	struct scic_sds_controller *scic,
	u32 timeout)
{
	enum sci_status status = SCI_FAILURE_INVALID_STATE;
	sci_base_controller_timed_handler_t stop;
	u32 state;

	state = scic->parent.state_machine.current_state_id;
	stop = scic_sds_controller_state_handler_table[state].base.stop;

	if (stop)
		status = stop(&scic->parent, timeout);
	else
		dev_warn(scic_to_dev(scic),
			 "%s: SCIC Controller stop operation requested in "
			 "invalid state %d\n",
			 __func__,
			 sci_base_state_machine_get_state(
				 scic_sds_controller_get_base_state_machine(
					 scic)));

	return status;
}

/* --------------------------------------------------------------------------- */

enum sci_status scic_controller_reset(
	struct scic_sds_controller *scic)
{
	enum sci_status status = SCI_FAILURE_INVALID_STATE;
	sci_base_controller_handler_t reset;
	u32 state;

	state = scic->parent.state_machine.current_state_id;
	reset = scic_sds_controller_state_handler_table[state].base.reset;

	if (reset)
		status = reset(&scic->parent);
	else
		dev_warn(scic_to_dev(scic),
			 "%s: SCIC Controller reset operation requested in "
			 "invalid state %d\n",
			 __func__,
			 sci_base_state_machine_get_state(
				 scic_sds_controller_get_base_state_machine(
					 scic)));

	return status;
}

enum sci_io_status scic_controller_start_io(
	struct scic_sds_controller *scic,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *io_request,
	u16 io_tag)
{
	u32 state;
	sci_base_controller_start_request_handler_t start_io;

	state = scic->parent.state_machine.current_state_id;
	start_io = scic_sds_controller_state_handler_table[state].base.start_io;

	return start_io(&scic->parent,
			(struct sci_base_remote_device *) remote_device,
			(struct sci_base_request *)io_request, io_tag);
}

/* --------------------------------------------------------------------------- */

enum sci_status scic_controller_terminate_request(
	struct scic_sds_controller *scic,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *request)
{
	sci_base_controller_request_handler_t terminate_request;
	u32 state;

	state = scic->parent.state_machine.current_state_id;
	terminate_request = scic_sds_controller_state_handler_table[state].terminate_request;

	return terminate_request(&scic->parent,
				 (struct sci_base_remote_device *)remote_device,
				 (struct sci_base_request *)request);
}

/* --------------------------------------------------------------------------- */

enum sci_status scic_controller_complete_io(
	struct scic_sds_controller *scic,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *io_request)
{
	u32 state;
	sci_base_controller_request_handler_t complete_io;

	state = scic->parent.state_machine.current_state_id;
	complete_io = scic_sds_controller_state_handler_table[state].base.complete_io;

	return complete_io(&scic->parent,
			   (struct sci_base_remote_device *)remote_device,
			   (struct sci_base_request *)io_request);
}

/* --------------------------------------------------------------------------- */


enum sci_task_status scic_controller_start_task(
	struct scic_sds_controller *scic,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *task_request,
	u16 task_tag)
{
	u32 state;
	sci_base_controller_start_request_handler_t start_task;
	enum sci_task_status status = SCI_TASK_FAILURE_INVALID_STATE;

	state = scic->parent.state_machine.current_state_id;
	start_task = scic_sds_controller_state_handler_table[state].base.start_task;

	if (start_task)
		status = start_task(&scic->parent,
				    (struct sci_base_remote_device *)remote_device,
				    (struct sci_base_request *)task_request,
				    task_tag);
	else
		dev_warn(scic_to_dev(scic),
			 "%s: SCIC Controller starting task from invalid "
			 "state\n",
			 __func__);

	return status;
}

/* --------------------------------------------------------------------------- */

enum sci_status scic_controller_complete_task(
	struct scic_sds_controller *scic,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *task_request)
{
	u32 state;
	sci_base_controller_request_handler_t complete_task;
	enum sci_status status = SCI_FAILURE_INVALID_STATE;

	state = scic->parent.state_machine.current_state_id;
	complete_task = scic_sds_controller_state_handler_table[state].base.complete_task;

	if (complete_task)
		status = complete_task(&scic->parent,
				       (struct sci_base_remote_device *)remote_device,
				       (struct sci_base_request *)task_request);
	else
		dev_warn(scic_to_dev(scic),
			 "%s: SCIC Controller completing task from invalid "
			 "state\n",
			 __func__);

	return status;
}


/* --------------------------------------------------------------------------- */

enum sci_status scic_controller_get_port_handle(
	struct scic_sds_controller *scic,
	u8 port_index,
	struct scic_sds_port **port_handle)
{
	if (port_index < scic->logical_port_entries) {
		*port_handle = &scic->port_table[port_index];

		return SCI_SUCCESS;
	}

	return SCI_FAILURE_INVALID_PORT;
}

/* --------------------------------------------------------------------------- */

enum sci_status scic_controller_get_phy_handle(
	struct scic_sds_controller *scic,
	u8 phy_index,
	struct scic_sds_phy **phy_handle)
{
	if (phy_index < ARRAY_SIZE(scic->phy_table)) {
		*phy_handle = &scic->phy_table[phy_index];

		return SCI_SUCCESS;
	}

	dev_err(scic_to_dev(scic),
		"%s: Controller:0x%p PhyId:0x%x invalid phy index\n",
		__func__, scic, phy_index);

	return SCI_FAILURE_INVALID_PHY;
}

/* --------------------------------------------------------------------------- */

u16 scic_controller_allocate_io_tag(
	struct scic_sds_controller *scic)
{
	u16 task_context;
	u16 sequence_count;

	if (!sci_pool_empty(scic->tci_pool)) {
		sci_pool_get(scic->tci_pool, task_context);

		sequence_count = scic->io_request_sequence[task_context];

		return scic_sds_io_tag_construct(sequence_count, task_context);
	}

	return SCI_CONTROLLER_INVALID_IO_TAG;
}

/* --------------------------------------------------------------------------- */

enum sci_status scic_controller_free_io_tag(
	struct scic_sds_controller *scic,
	u16 io_tag)
{
	u16 sequence;
	u16 index;

	BUG_ON(io_tag == SCI_CONTROLLER_INVALID_IO_TAG);

	sequence = scic_sds_io_tag_get_sequence(io_tag);
	index    = scic_sds_io_tag_get_index(io_tag);

	if (!sci_pool_full(scic->tci_pool)) {
		if (sequence == scic->io_request_sequence[index]) {
			scic_sds_io_sequence_increment(
				scic->io_request_sequence[index]);

			sci_pool_put(scic->tci_pool, index);

			return SCI_SUCCESS;
		}
	}

	return SCI_FAILURE_INVALID_IO_TAG;
}

/* --------------------------------------------------------------------------- */

void scic_controller_enable_interrupts(
	struct scic_sds_controller *scic)
{
	BUG_ON(scic->smu_registers == NULL);
	SMU_IMR_WRITE(scic, 0x00000000);
}

/* --------------------------------------------------------------------------- */

void scic_controller_disable_interrupts(
	struct scic_sds_controller *scic)
{
	BUG_ON(scic->smu_registers == NULL);
	SMU_IMR_WRITE(scic, 0xffffffff);
}

/* --------------------------------------------------------------------------- */

enum sci_status scic_controller_set_mode(
	struct scic_sds_controller *scic,
	enum sci_controller_mode operating_mode)
{
	enum sci_status status          = SCI_SUCCESS;

	if ((scic->parent.state_machine.current_state_id ==
				SCI_BASE_CONTROLLER_STATE_INITIALIZING) ||
	    (scic->parent.state_machine.current_state_id ==
				SCI_BASE_CONTROLLER_STATE_INITIALIZED)) {
		switch (operating_mode) {
		case SCI_MODE_SPEED:
			scic->remote_node_entries      = SCI_MAX_REMOTE_DEVICES;
			scic->task_context_entries     = SCU_IO_REQUEST_COUNT;
			scic->uf_control.buffers.count =
				SCU_UNSOLICITED_FRAME_COUNT;
			scic->completion_event_entries = SCU_EVENT_COUNT;
			scic->completion_queue_entries =
				SCU_COMPLETION_QUEUE_COUNT;
			scic_sds_controller_build_memory_descriptor_table(scic);
			break;

		case SCI_MODE_SIZE:
			scic->remote_node_entries      = SCI_MIN_REMOTE_DEVICES;
			scic->task_context_entries     = SCI_MIN_IO_REQUESTS;
			scic->uf_control.buffers.count =
				SCU_MIN_UNSOLICITED_FRAMES;
			scic->completion_event_entries = SCU_MIN_EVENTS;
			scic->completion_queue_entries =
				SCU_MIN_COMPLETION_QUEUE_ENTRIES;
			scic_sds_controller_build_memory_descriptor_table(scic);
			break;

		default:
			status = SCI_FAILURE_INVALID_PARAMETER_VALUE;
			break;
		}
	} else
		status = SCI_FAILURE_INVALID_STATE;

	return status;
}

/**
 * scic_sds_controller_reset_hardware() -
 *
 * This method will reset the controller hardware.
 */
void scic_sds_controller_reset_hardware(
	struct scic_sds_controller *scic)
{
	/* Disable interrupts so we dont take any spurious interrupts */
	scic_controller_disable_interrupts(scic);

	/* Reset the SCU */
	SMU_SMUSRCR_WRITE(scic, 0xFFFFFFFF);

	/* Delay for 1ms to before clearing the CQP and UFQPR. */
	udelay(1000);

	/* The write to the CQGR clears the CQP */
	SMU_CQGR_WRITE(scic, 0x00000000);

	/* The write to the UFQGP clears the UFQPR */
	SCU_UFQGP_WRITE(scic, 0x00000000);
}

/* --------------------------------------------------------------------------- */

enum sci_status scic_user_parameters_set(
	struct scic_sds_controller *scic,
	union scic_user_parameters *scic_parms)
{
	if (
		(scic->parent.state_machine.current_state_id
		 == SCI_BASE_CONTROLLER_STATE_RESET)
		|| (scic->parent.state_machine.current_state_id
		    == SCI_BASE_CONTROLLER_STATE_INITIALIZING)
		|| (scic->parent.state_machine.current_state_id
		    == SCI_BASE_CONTROLLER_STATE_INITIALIZED)
		) {
		u16 index;

		/*
		 * Validate the user parameters.  If they are not legal, then
		 * return a failure. */
		for (index = 0; index < SCI_MAX_PHYS; index++) {
			if (!(scic_parms->sds1.phys[index].max_speed_generation
			     <= SCIC_SDS_PARM_MAX_SPEED
			     && scic_parms->sds1.phys[index].max_speed_generation
			     > SCIC_SDS_PARM_NO_SPEED))
				return SCI_FAILURE_INVALID_PARAMETER_VALUE;

			if (scic_parms->sds1.phys[index].in_connection_align_insertion_frequency < 3)
				return SCI_FAILURE_INVALID_PARAMETER_VALUE;
			if (
			    (scic_parms->sds1.phys[index].in_connection_align_insertion_frequency < 3) ||
			    (scic_parms->sds1.phys[index].align_insertion_frequency == 0) ||
			    (scic_parms->sds1.phys[index].notify_enable_spin_up_insertion_frequency == 0)
			    )
				return SCI_FAILURE_INVALID_PARAMETER_VALUE;
		}

		if ((scic_parms->sds1.stp_inactivity_timeout == 0) ||
		   (scic_parms->sds1.ssp_inactivity_timeout == 0) ||
		   (scic_parms->sds1.stp_max_occupancy_timeout == 0) ||
		   (scic_parms->sds1.ssp_max_occupancy_timeout == 0) ||
		   (scic_parms->sds1.no_outbound_task_timeout == 0))
			return SCI_FAILURE_INVALID_PARAMETER_VALUE;

		memcpy(&scic->user_parameters, scic_parms, sizeof(*scic_parms));

		return SCI_SUCCESS;
	}

	return SCI_FAILURE_INVALID_STATE;
}

/* --------------------------------------------------------------------------- */

void scic_user_parameters_get(
	struct scic_sds_controller *scic,
	union scic_user_parameters *scic_parms)
{
	memcpy(scic_parms, (&scic->user_parameters), sizeof(*scic_parms));
}

/* --------------------------------------------------------------------------- */

enum sci_status scic_oem_parameters_set(
	struct scic_sds_controller *scic,
	union scic_oem_parameters *scic_parms)
{
	if (
		(scic->parent.state_machine.current_state_id
		 == SCI_BASE_CONTROLLER_STATE_RESET)
		|| (scic->parent.state_machine.current_state_id
		    == SCI_BASE_CONTROLLER_STATE_INITIALIZING)
		|| (scic->parent.state_machine.current_state_id
		    == SCI_BASE_CONTROLLER_STATE_INITIALIZED)
		) {
		u16 index;

		/*
		 * Validate the oem parameters.  If they are not legal, then
		 * return a failure. */
		for (index = 0; index < SCI_MAX_PORTS; index++) {
			if (scic_parms->sds1.ports[index].phy_mask > SCIC_SDS_PARM_PHY_MASK_MAX) {
				return SCI_FAILURE_INVALID_PARAMETER_VALUE;
			}
		}

		for (index = 0; index < SCI_MAX_PHYS; index++) {
			if (
				scic_parms->sds1.phys[index].sas_address.high == 0
				&& scic_parms->sds1.phys[index].sas_address.low  == 0
				) {
				return SCI_FAILURE_INVALID_PARAMETER_VALUE;
			}
		}

		memcpy(&scic->oem_parameters, scic_parms, sizeof(*scic_parms));
		return SCI_SUCCESS;
	}

	return SCI_FAILURE_INVALID_STATE;
}

/* --------------------------------------------------------------------------- */

void scic_oem_parameters_get(
	struct scic_sds_controller *scic,
	union scic_oem_parameters *scic_parms)
{
	memcpy(scic_parms, (&scic->oem_parameters), sizeof(*scic_parms));
}

/* --------------------------------------------------------------------------- */


#define INTERRUPT_COALESCE_TIMEOUT_BASE_RANGE_LOWER_BOUND_NS 853
#define INTERRUPT_COALESCE_TIMEOUT_BASE_RANGE_UPPER_BOUND_NS 1280
#define INTERRUPT_COALESCE_TIMEOUT_MAX_US                    2700000
#define INTERRUPT_COALESCE_NUMBER_MAX                        256
#define INTERRUPT_COALESCE_TIMEOUT_ENCODE_MIN                7
#define INTERRUPT_COALESCE_TIMEOUT_ENCODE_MAX                28

enum sci_status scic_controller_set_interrupt_coalescence(
	struct scic_sds_controller *scic_controller,
	u32 coalesce_number,
	u32 coalesce_timeout)
{
	u8 timeout_encode = 0;
	u32 min = 0;
	u32 max = 0;

	/* Check if the input parameters fall in the range. */
	if (coalesce_number > INTERRUPT_COALESCE_NUMBER_MAX)
		return SCI_FAILURE_INVALID_PARAMETER_VALUE;

	/*
	 *  Defined encoding for interrupt coalescing timeout:
	 *              Value   Min      Max     Units
	 *              -----   ---      ---     -----
	 *              0       -        -       Disabled
	 *              1       13.3     20.0    ns
	 *              2       26.7     40.0
	 *              3       53.3     80.0
	 *              4       106.7    160.0
	 *              5       213.3    320.0
	 *              6       426.7    640.0
	 *              7       853.3    1280.0
	 *              8       1.7      2.6     us
	 *              9       3.4      5.1
	 *              10      6.8      10.2
	 *              11      13.7     20.5
	 *              12      27.3     41.0
	 *              13      54.6     81.9
	 *              14      109.2    163.8
	 *              15      218.5    327.7
	 *              16      436.9    655.4
	 *              17      873.8    1310.7
	 *              18      1.7      2.6     ms
	 *              19      3.5      5.2
	 *              20      7.0      10.5
	 *              21      14.0     21.0
	 *              22      28.0     41.9
	 *              23      55.9     83.9
	 *              24      111.8    167.8
	 *              25      223.7    335.5
	 *              26      447.4    671.1
	 *              27      894.8    1342.2
	 *              28      1.8      2.7     s
	 *              Others Undefined */

	/*
	 * Use the table above to decide the encode of interrupt coalescing timeout
	 * value for register writing. */
	if (coalesce_timeout == 0)
		timeout_encode = 0;
	else{
		/* make the timeout value in unit of (10 ns). */
		coalesce_timeout = coalesce_timeout * 100;
		min = INTERRUPT_COALESCE_TIMEOUT_BASE_RANGE_LOWER_BOUND_NS / 10;
		max = INTERRUPT_COALESCE_TIMEOUT_BASE_RANGE_UPPER_BOUND_NS / 10;

		/* get the encode of timeout for register writing. */
		for (timeout_encode = INTERRUPT_COALESCE_TIMEOUT_ENCODE_MIN;
		      timeout_encode <= INTERRUPT_COALESCE_TIMEOUT_ENCODE_MAX;
		      timeout_encode++) {
			if (min <= coalesce_timeout &&  max > coalesce_timeout)
				break;
			else if (coalesce_timeout >= max && coalesce_timeout < min * 2
				 && coalesce_timeout <= INTERRUPT_COALESCE_TIMEOUT_MAX_US * 100) {
				if ((coalesce_timeout - max) < (2 * min - coalesce_timeout))
					break;
				else{
					timeout_encode++;
					break;
				}
			} else {
				max = max * 2;
				min = min * 2;
			}
		}

		if (timeout_encode == INTERRUPT_COALESCE_TIMEOUT_ENCODE_MAX + 1)
			/* the value is out of range. */
			return SCI_FAILURE_INVALID_PARAMETER_VALUE;
	}

	SMU_ICC_WRITE(
		scic_controller,
		(SMU_ICC_GEN_VAL(NUMBER, coalesce_number) |
		 SMU_ICC_GEN_VAL(TIMER, timeout_encode))
		);

	scic_controller->interrupt_coalesce_number = (u16)coalesce_number;
	scic_controller->interrupt_coalesce_timeout = coalesce_timeout / 100;

	return SCI_SUCCESS;
}


struct scic_sds_controller *scic_controller_alloc(struct device *dev)
{
	return devm_kzalloc(dev, sizeof(struct scic_sds_controller), GFP_KERNEL);
}

/*
 * *****************************************************************************
 * * DEFAULT STATE HANDLERS
 * ***************************************************************************** */

/**
 *
 * @controller: This is struct sci_base_controller object which is cast into a
 *    struct scic_sds_controller object.
 * @remote_device: This is struct sci_base_remote_device which, if it was used, would
 *    be cast to a struct scic_sds_remote_device.
 * @io_request: This is the struct sci_base_request which, if it was used, would be
 *    cast to a SCIC_SDS_IO_REQUEST.
 * @io_tag: This is the IO tag to be assigned to the IO request or
 *    SCI_CONTROLLER_INVALID_IO_TAG.
 *
 * This method is called when the struct scic_sds_controller default start io/task
 * handler is in place. - Issue a warning message enum sci_status
 * SCI_FAILURE_INVALID_STATE
 */
static enum sci_status scic_sds_controller_default_start_operation_handler(
	struct sci_base_controller *controller,
	struct sci_base_remote_device *remote_device,
	struct sci_base_request *io_request,
	u16 io_tag)
{
	struct scic_sds_controller *this_controller;

	this_controller = (struct scic_sds_controller *)controller;

	dev_warn(scic_to_dev(this_controller),
		 "%s: SCIC Controller requested to start an io/task from "
		 "invalid state %d\n",
		 __func__,
		 sci_base_state_machine_get_state(
			 scic_sds_controller_get_base_state_machine(
				 this_controller)));

	return SCI_FAILURE_INVALID_STATE;
}

/**
 *
 * @controller: This is struct sci_base_controller object which is cast into a
 *    struct scic_sds_controller object.
 * @remote_device: This is struct sci_base_remote_device which, if it was used, would
 *    be cast to a struct scic_sds_remote_device.
 * @io_request: This is the struct sci_base_request which, if it was used, would be
 *    cast to a SCIC_SDS_IO_REQUEST.
 *
 * This method is called when the struct scic_sds_controller default request handler
 * is in place. - Issue a warning message enum sci_status SCI_FAILURE_INVALID_STATE
 */
static enum sci_status scic_sds_controller_default_request_handler(
	struct sci_base_controller *controller,
	struct sci_base_remote_device *remote_device,
	struct sci_base_request *io_request)
{
	struct scic_sds_controller *this_controller;

	this_controller = (struct scic_sds_controller *)controller;

	dev_warn(scic_to_dev(this_controller),
		"%s: SCIC Controller request operation from invalid state %d\n",
		__func__,
		sci_base_state_machine_get_state(
			scic_sds_controller_get_base_state_machine(
				this_controller)));

	return SCI_FAILURE_INVALID_STATE;
}

/*
 * *****************************************************************************
 * * GENERAL (COMMON) STATE HANDLERS
 * ***************************************************************************** */

/**
 *
 * @controller: The struct sci_base_controller object which is cast into a
 *    struct scic_sds_controller object.
 *
 * This method is called when the struct scic_sds_controller is in the ready state
 * reset handler is in place. - Transition to
 * SCI_BASE_CONTROLLER_STATE_RESETTING enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_controller_general_reset_handler(
	struct sci_base_controller *controller)
{
	struct scic_sds_controller *this_controller;

	this_controller = (struct scic_sds_controller *)controller;

	/*
	 * The reset operation is not a graceful cleanup just perform the state
	 * transition. */
	sci_base_state_machine_change_state(
		scic_sds_controller_get_base_state_machine(this_controller),
		SCI_BASE_CONTROLLER_STATE_RESETTING
		);

	return SCI_SUCCESS;
}

/*
 * *****************************************************************************
 * * RESET STATE HANDLERS
 * ***************************************************************************** */

/**
 *
 * @controller: This is the struct sci_base_controller object which is cast into a
 *    struct scic_sds_controller object.
 *
 * This method is the struct scic_sds_controller initialize handler for the reset
 * state. - Currently this function does nothing enum sci_status SCI_FAILURE This
 * function is not yet implemented and is a valid request from the reset state.
 */
static enum sci_status scic_sds_controller_reset_state_initialize_handler(
	struct sci_base_controller *controller)
{
	u32 index;
	enum sci_status result = SCI_SUCCESS;
	struct scic_sds_controller *this_controller;

	this_controller = (struct scic_sds_controller *)controller;

	sci_base_state_machine_change_state(
		scic_sds_controller_get_base_state_machine(this_controller),
		SCI_BASE_CONTROLLER_STATE_INITIALIZING
		);

	this_controller->timeout_timer = isci_event_timer_create(
		this_controller,
		(void (*)(void *))scic_sds_controller_timeout_handler,
		(void (*)(void *))controller);

	scic_sds_controller_initialize_phy_startup(this_controller);

	scic_sds_controller_initialize_power_control(this_controller);

	/*
	 * There is nothing to do here for B0 since we do not have to
	 * program the AFE registers.
	 * / @todo The AFE settings are supposed to be correct for the B0 but
	 * /       presently they seem to be wrong. */
	scic_sds_controller_afe_initialization(this_controller);

	if (SCI_SUCCESS == result) {
		u32 status;
		u32 terminate_loop;

		/* Take the hardware out of reset */
		SMU_SMUSRCR_WRITE(this_controller, 0x00000000);

		/*
		 * / @todo Provide meaningfull error code for hardware failure
		 * result = SCI_FAILURE_CONTROLLER_HARDWARE; */
		result = SCI_FAILURE;
		terminate_loop = 100;

		while (terminate_loop-- && (result != SCI_SUCCESS)) {
			/* Loop until the hardware reports success */
			udelay(SCU_CONTEXT_RAM_INIT_STALL_TIME);
			status = SMU_SMUCSR_READ(this_controller);

			if ((status & SCU_RAM_INIT_COMPLETED) == SCU_RAM_INIT_COMPLETED) {
				result = SCI_SUCCESS;
			}
		}
	}

	if (result == SCI_SUCCESS) {
		u32 max_supported_ports;
		u32 max_supported_devices;
		u32 max_supported_io_requests;
		u32 device_context_capacity;

		/*
		 * Determine what are the actaul device capacities that the
		 * hardware will support */
		device_context_capacity = SMU_DCC_READ(this_controller);

		max_supported_ports =
			smu_dcc_get_max_ports(device_context_capacity);
		max_supported_devices =
			smu_dcc_get_max_remote_node_context(device_context_capacity);
		max_supported_io_requests =
			smu_dcc_get_max_task_context(device_context_capacity);

		/* Make all PEs that are unassigned match up with the logical ports */
		for (index = 0; index < max_supported_ports; index++) {
			scu_register_write(
				this_controller,
				this_controller->scu_registers->peg0.ptsg.protocol_engine[index],
				index
				);
		}

		/* Record the smaller of the two capacity values */
		this_controller->logical_port_entries =
			min(max_supported_ports, this_controller->logical_port_entries);

		this_controller->task_context_entries =
			min(max_supported_io_requests, this_controller->task_context_entries);

		this_controller->remote_node_entries =
			min(max_supported_devices, this_controller->remote_node_entries);

		/*
		 * Now that we have the correct hardware reported minimum values
		 * build the MDL for the controller.  Default to a performance
		 * configuration. */
		scic_controller_set_mode(this_controller, SCI_MODE_SPEED);
	}

	/* Initialize hardware PCI Relaxed ordering in DMA engines */
	if (result == SCI_SUCCESS) {
		u32 dma_configuration;

		/* Configure the payload DMA */
		dma_configuration = SCU_PDMACR_READ(this_controller);
		dma_configuration |= SCU_PDMACR_GEN_BIT(PCI_RELAXED_ORDERING_ENABLE);
		SCU_PDMACR_WRITE(this_controller, dma_configuration);

		/* Configure the control DMA */
		dma_configuration = SCU_CDMACR_READ(this_controller);
		dma_configuration |= SCU_CDMACR_GEN_BIT(PCI_RELAXED_ORDERING_ENABLE);
		SCU_CDMACR_WRITE(this_controller, dma_configuration);
	}

	/*
	 * Initialize the PHYs before the PORTs because the PHY registers
	 * are accessed during the port initialization. */
	if (result == SCI_SUCCESS) {
		/* Initialize the phys */
		for (index = 0;
		     (result == SCI_SUCCESS) && (index < SCI_MAX_PHYS);
		     index++) {
			result = scic_sds_phy_initialize(
				&this_controller->phy_table[index],
				&this_controller->scu_registers->peg0.pe[index].tl,
				&this_controller->scu_registers->peg0.pe[index].ll
				);
		}
	}

	if (result == SCI_SUCCESS) {
		/* Initialize the logical ports */
		for (index = 0;
		     (index < this_controller->logical_port_entries)
		     && (result == SCI_SUCCESS);
		     index++) {
			result = scic_sds_port_initialize(
				&this_controller->port_table[index],
				&this_controller->scu_registers->peg0.ptsg.port[index],
				&this_controller->scu_registers->peg0.ptsg.protocol_engine,
				&this_controller->scu_registers->peg0.viit[index]
				);
		}
	}

	if (SCI_SUCCESS == result) {
		result = scic_sds_port_configuration_agent_initialize(
			this_controller,
			&this_controller->port_agent
			);
	}

	/* Advance the controller state machine */
	if (result == SCI_SUCCESS) {
		sci_base_state_machine_change_state(
			scic_sds_controller_get_base_state_machine(this_controller),
			SCI_BASE_CONTROLLER_STATE_INITIALIZED
			);
	} else {
		sci_base_state_machine_change_state(
			scic_sds_controller_get_base_state_machine(this_controller),
			SCI_BASE_CONTROLLER_STATE_FAILED
			);
	}

	return result;
}

/*
 * *****************************************************************************
 * * INITIALIZED STATE HANDLERS
 * ***************************************************************************** */

/**
 *
 * @controller: This is the struct sci_base_controller object which is cast into a
 *    struct scic_sds_controller object.
 * @timeout: This is the allowed time for the controller object to reach the
 *    started state.
 *
 * This method is the struct scic_sds_controller start handler for the initialized
 * state. - Validate we have a good memory descriptor table - Initialze the
 * physical memory before programming the hardware - Program the SCU hardware
 * with the physical memory addresses passed in the memory descriptor table. -
 * Initialzie the TCi pool - Initialize the RNi pool - Initialize the
 * completion queue - Initialize the unsolicited frame data - Take the SCU port
 * task scheduler out of reset - Start the first phy object. - Transition to
 * SCI_BASE_CONTROLLER_STATE_STARTING. enum sci_status SCI_SUCCESS if all of the
 * controller start operations complete
 * SCI_FAILURE_UNSUPPORTED_INFORMATION_FIELD if one or more of the memory
 * descriptor fields is invalid.
 */
static enum sci_status scic_sds_controller_initialized_state_start_handler(
	struct sci_base_controller *controller,
	u32 timeout)
{
	u16 index;
	enum sci_status result;
	struct scic_sds_controller *this_controller;

	this_controller = (struct scic_sds_controller *)controller;

	/* Make sure that the SCI User filled in the memory descriptor table correctly */
	result = scic_sds_controller_validate_memory_descriptor_table(this_controller);

	if (result == SCI_SUCCESS) {
		/* The memory descriptor list looks good so program the hardware */
		scic_sds_controller_ram_initialization(this_controller);
	}

	if (SCI_SUCCESS == result) {
		/* Build the TCi free pool */
		sci_pool_initialize(this_controller->tci_pool);
		for (index = 0; index < this_controller->task_context_entries; index++) {
			sci_pool_put(this_controller->tci_pool, index);
		}

		/* Build the RNi free pool */
		scic_sds_remote_node_table_initialize(
			&this_controller->available_remote_nodes,
			this_controller->remote_node_entries
			);
	}

	if (SCI_SUCCESS == result) {
		/*
		 * Before anything else lets make sure we will not be interrupted
		 * by the hardware. */
		scic_controller_disable_interrupts(this_controller);

		/* Enable the port task scheduler */
		scic_sds_controller_enable_port_task_scheduler(this_controller);

		/* Assign all the task entries to this controller physical function */
		scic_sds_controller_assign_task_entries(this_controller);

		/* Now initialze the completion queue */
		scic_sds_controller_initialize_completion_queue(this_controller);

		/* Initialize the unsolicited frame queue for use */
		scic_sds_controller_initialize_unsolicited_frame_queue(this_controller);
	}

	if (SCI_SUCCESS == result) {
		scic_sds_controller_start_next_phy(this_controller);

		isci_event_timer_start(this_controller,
				    this_controller->timeout_timer,
				    timeout);

		sci_base_state_machine_change_state(
			scic_sds_controller_get_base_state_machine(this_controller),
			SCI_BASE_CONTROLLER_STATE_STARTING
			);
	}

	return result;
}

/*
 * *****************************************************************************
 * * INITIALIZED STATE HANDLERS
 * ***************************************************************************** */

/**
 *
 * @controller: This is struct scic_sds_controller which receives the link up
 *    notification.
 * @port: This is struct scic_sds_port with which the phy is associated.
 * @phy: This is the struct scic_sds_phy which has gone link up.
 *
 * This method is called when the struct scic_sds_controller is in the starting state
 * link up handler is called.  This method will perform the following: - Stop
 * the phy timer - Start the next phy - Report the link up condition to the
 * port object none
 */
static void scic_sds_controller_starting_state_link_up_handler(
	struct scic_sds_controller *this_controller,
	struct scic_sds_port *port,
	struct scic_sds_phy *phy)
{
	scic_sds_controller_phy_timer_stop(this_controller);

	this_controller->port_agent.link_up_handler(
		this_controller, &this_controller->port_agent, port, phy
		);
	/* scic_sds_port_link_up(port, phy); */

	scic_sds_controller_start_next_phy(this_controller);
}

/**
 *
 * @controller: This is struct scic_sds_controller which receives the link down
 *    notification.
 * @port: This is struct scic_sds_port with which the phy is associated.
 * @phy: This is the struct scic_sds_phy which has gone link down.
 *
 * This method is called when the struct scic_sds_controller is in the starting state
 * link down handler is called. - Report the link down condition to the port
 * object none
 */
static void scic_sds_controller_starting_state_link_down_handler(
	struct scic_sds_controller *this_controller,
	struct scic_sds_port *port,
	struct scic_sds_phy *phy)
{
	this_controller->port_agent.link_down_handler(
		this_controller, &this_controller->port_agent, port, phy
		);
	/* scic_sds_port_link_down(port, phy); */
}

/*
 * *****************************************************************************
 * * READY STATE HANDLERS
 * ***************************************************************************** */

/**
 *
 * @controller: The struct sci_base_controller object which is cast into a
 *    struct scic_sds_controller object.
 * @timeout: The timeout for when the stop operation should report a failure.
 *
 * This method is called when the struct scic_sds_controller is in the ready state
 * stop handler is called. - Start the timeout timer - Transition to
 * SCI_BASE_CONTROLLER_STATE_STOPPING. enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_controller_ready_state_stop_handler(
	struct sci_base_controller *controller,
	u32 timeout)
{
	struct scic_sds_controller *this_controller;

	this_controller = (struct scic_sds_controller *)controller;

	isci_event_timer_start(this_controller,
			    this_controller->timeout_timer,
			    timeout);

	sci_base_state_machine_change_state(
		scic_sds_controller_get_base_state_machine(this_controller),
		SCI_BASE_CONTROLLER_STATE_STOPPING
		);

	return SCI_SUCCESS;
}

/**
 *
 * @controller: This is struct sci_base_controller object which is cast into a
 *    struct scic_sds_controller object.
 * @remote_device: This is struct sci_base_remote_device which is cast to a
 *    struct scic_sds_remote_device object.
 * @io_request: This is the struct sci_base_request which is cast to a
 *    SCIC_SDS_IO_REQUEST object.
 * @io_tag: This is the IO tag to be assigned to the IO request or
 *    SCI_CONTROLLER_INVALID_IO_TAG.
 *
 * This method is called when the struct scic_sds_controller is in the ready state and
 * the start io handler is called. - Start the io request on the remote device
 * - if successful - assign the io_request to the io_request_table - post the
 * request to the hardware enum sci_status SCI_SUCCESS if the start io operation
 * succeeds SCI_FAILURE_INSUFFICIENT_RESOURCES if the IO tag could not be
 * allocated for the io request. SCI_FAILURE_INVALID_STATE if one or more
 * objects are not in a valid state to accept io requests. How does the io_tag
 * parameter get assigned to the io request?
 */
static enum sci_status scic_sds_controller_ready_state_start_io_handler(
	struct sci_base_controller *controller,
	struct sci_base_remote_device *remote_device,
	struct sci_base_request *io_request,
	u16 io_tag)
{
	enum sci_status status;

	struct scic_sds_controller *this_controller;
	struct scic_sds_request *the_request;
	struct scic_sds_remote_device *the_device;

	this_controller = (struct scic_sds_controller *)controller;
	the_request = (struct scic_sds_request *)io_request;
	the_device = (struct scic_sds_remote_device *)remote_device;

	status = scic_sds_remote_device_start_io(this_controller, the_device, the_request);

	if (status == SCI_SUCCESS) {
		this_controller->io_request_table[
			scic_sds_io_tag_get_index(the_request->io_tag)] = the_request;

		scic_sds_controller_post_request(
			this_controller,
			scic_sds_request_get_post_context(the_request)
			);
	}

	return status;
}

/**
 *
 * @controller: This is struct sci_base_controller object which is cast into a
 *    struct scic_sds_controller object.
 * @remote_device: This is struct sci_base_remote_device which is cast to a
 *    struct scic_sds_remote_device object.
 * @io_request: This is the struct sci_base_request which is cast to a
 *    SCIC_SDS_IO_REQUEST object.
 *
 * This method is called when the struct scic_sds_controller is in the ready state and
 * the complete io handler is called. - Complete the io request on the remote
 * device - if successful - remove the io_request to the io_request_table
 * enum sci_status SCI_SUCCESS if the start io operation succeeds
 * SCI_FAILURE_INVALID_STATE if one or more objects are not in a valid state to
 * accept io requests.
 */
static enum sci_status scic_sds_controller_ready_state_complete_io_handler(
	struct sci_base_controller *controller,
	struct sci_base_remote_device *remote_device,
	struct sci_base_request *io_request)
{
	u16 index;
	enum sci_status status;
	struct scic_sds_controller *this_controller;
	struct scic_sds_request *the_request;
	struct scic_sds_remote_device *the_device;

	this_controller = (struct scic_sds_controller *)controller;
	the_request = (struct scic_sds_request *)io_request;
	the_device = (struct scic_sds_remote_device *)remote_device;

	status = scic_sds_remote_device_complete_io(
		this_controller, the_device, the_request);

	if (status == SCI_SUCCESS) {
		index = scic_sds_io_tag_get_index(the_request->io_tag);
		this_controller->io_request_table[index] = NULL;
	}

	return status;
}

/**
 *
 * @controller: This is struct sci_base_controller object which is cast into a
 *    struct scic_sds_controller object.
 * @remote_device: This is struct sci_base_remote_device which is cast to a
 *    struct scic_sds_remote_device object.
 * @io_request: This is the struct sci_base_request which is cast to a
 *    SCIC_SDS_IO_REQUEST object.
 *
 * This method is called when the struct scic_sds_controller is in the ready state and
 * the continue io handler is called. enum sci_status
 */
static enum sci_status scic_sds_controller_ready_state_continue_io_handler(
	struct sci_base_controller *controller,
	struct sci_base_remote_device *remote_device,
	struct sci_base_request *io_request)
{
	struct scic_sds_controller *this_controller;
	struct scic_sds_request *the_request;

	the_request     = (struct scic_sds_request *)io_request;
	this_controller = (struct scic_sds_controller *)controller;

	this_controller->io_request_table[
		scic_sds_io_tag_get_index(the_request->io_tag)] = the_request;

	scic_sds_controller_post_request(
		this_controller,
		scic_sds_request_get_post_context(the_request)
		);

	return SCI_SUCCESS;
}

/**
 *
 * @controller: This is struct sci_base_controller object which is cast into a
 *    struct scic_sds_controller object.
 * @remote_device: This is struct sci_base_remote_device which is cast to a
 *    struct scic_sds_remote_device object.
 * @io_request: This is the struct sci_base_request which is cast to a
 *    SCIC_SDS_IO_REQUEST object.
 * @task_tag: This is the task tag to be assigned to the task request or
 *    SCI_CONTROLLER_INVALID_IO_TAG.
 *
 * This method is called when the struct scic_sds_controller is in the ready state and
 * the start task handler is called. - The remote device is requested to start
 * the task request - if successful - assign the task to the io_request_table -
 * post the request to the SCU hardware enum sci_status SCI_SUCCESS if the start io
 * operation succeeds SCI_FAILURE_INSUFFICIENT_RESOURCES if the IO tag could
 * not be allocated for the io request. SCI_FAILURE_INVALID_STATE if one or
 * more objects are not in a valid state to accept io requests. How does the io
 * tag get assigned in this code path?
 */
static enum sci_status scic_sds_controller_ready_state_start_task_handler(
	struct sci_base_controller *controller,
	struct sci_base_remote_device *remote_device,
	struct sci_base_request *io_request,
	u16 task_tag)
{
	struct scic_sds_controller *this_controller = (struct scic_sds_controller *)
						 controller;
	struct scic_sds_request *the_request     = (struct scic_sds_request *)
					      io_request;
	struct scic_sds_remote_device *the_device      = (struct scic_sds_remote_device *)
						    remote_device;
	enum sci_status status;

	status = scic_sds_remote_device_start_task(
		this_controller, the_device, the_request
		);

	if (status == SCI_SUCCESS) {
		this_controller->io_request_table[
			scic_sds_io_tag_get_index(the_request->io_tag)] = the_request;

		scic_sds_controller_post_request(
			this_controller,
			scic_sds_request_get_post_context(the_request)
			);
	} else if (status == SCI_FAILURE_RESET_DEVICE_PARTIAL_SUCCESS) {
		this_controller->io_request_table[
			scic_sds_io_tag_get_index(the_request->io_tag)] = the_request;

		/*
		 * We will let framework know this task request started successfully,
		 * although core is still woring on starting the request (to post tc when
		 * RNC is resumed.) */
		status = SCI_SUCCESS;
	}
	return status;
}

/**
 *
 * @controller: This is struct sci_base_controller object which is cast into a
 *    struct scic_sds_controller object.
 * @remote_device: This is struct sci_base_remote_device which is cast to a
 *    struct scic_sds_remote_device object.
 * @io_request: This is the struct sci_base_request which is cast to a
 *    SCIC_SDS_IO_REQUEST object.
 *
 * This method is called when the struct scic_sds_controller is in the ready state and
 * the terminate request handler is called. - call the io request terminate
 * function - if successful - post the terminate request to the SCU hardware
 * enum sci_status SCI_SUCCESS if the start io operation succeeds
 * SCI_FAILURE_INVALID_STATE if one or more objects are not in a valid state to
 * accept io requests.
 */
static enum sci_status scic_sds_controller_ready_state_terminate_request_handler(
	struct sci_base_controller *controller,
	struct sci_base_remote_device *remote_device,
	struct sci_base_request *io_request)
{
	struct scic_sds_controller *this_controller = (struct scic_sds_controller *)
						 controller;
	struct scic_sds_request *the_request     = (struct scic_sds_request *)
					      io_request;
	enum sci_status status;

	status = scic_sds_io_request_terminate(the_request);
	if (status == SCI_SUCCESS) {
		/*
		 * Utilize the original post context command and or in the POST_TC_ABORT
		 * request sub-type. */
		scic_sds_controller_post_request(
			this_controller,
			scic_sds_request_get_post_context(the_request)
			| SCU_CONTEXT_COMMAND_REQUEST_POST_TC_ABORT
			);
	}

	return status;
}

/**
 *
 * @controller: This is struct scic_sds_controller which receives the link up
 *    notification.
 * @port: This is struct scic_sds_port with which the phy is associated.
 * @phy: This is the struct scic_sds_phy which has gone link up.
 *
 * This method is called when the struct scic_sds_controller is in the starting state
 * link up handler is called.  This method will perform the following: - Stop
 * the phy timer - Start the next phy - Report the link up condition to the
 * port object none
 */
static void scic_sds_controller_ready_state_link_up_handler(
	struct scic_sds_controller *this_controller,
	struct scic_sds_port *port,
	struct scic_sds_phy *phy)
{
	this_controller->port_agent.link_up_handler(
		this_controller, &this_controller->port_agent, port, phy
		);
}

/**
 *
 * @controller: This is struct scic_sds_controller which receives the link down
 *    notification.
 * @port: This is struct scic_sds_port with which the phy is associated.
 * @phy: This is the struct scic_sds_phy which has gone link down.
 *
 * This method is called when the struct scic_sds_controller is in the starting state
 * link down handler is called. - Report the link down condition to the port
 * object none
 */
static void scic_sds_controller_ready_state_link_down_handler(
	struct scic_sds_controller *this_controller,
	struct scic_sds_port *port,
	struct scic_sds_phy *phy)
{
	this_controller->port_agent.link_down_handler(
		this_controller, &this_controller->port_agent, port, phy
		);
}

/*
 * *****************************************************************************
 * * STOPPING STATE HANDLERS
 * ***************************************************************************** */

/**
 *
 * @controller: This is struct sci_base_controller object which is cast into a
 *    struct scic_sds_controller object.
 * @remote_device: This is struct sci_base_remote_device which is cast to a
 *    struct scic_sds_remote_device object.
 * @io_request: This is the struct sci_base_request which is cast to a
 *    SCIC_SDS_IO_REQUEST object.
 *
 * This method is called when the struct scic_sds_controller is in a stopping state
 * and the complete io handler is called. - This function is not yet
 * implemented enum sci_status SCI_FAILURE
 */
static enum sci_status scic_sds_controller_stopping_state_complete_io_handler(
	struct sci_base_controller *controller,
	struct sci_base_remote_device *remote_device,
	struct sci_base_request *io_request)
{
	struct scic_sds_controller *this_controller;

	this_controller = (struct scic_sds_controller *)controller;

	/* / @todo Implement this function */
	return SCI_FAILURE;
}

/**
 *
 * @controller: This is struct sci_base_controller object which is cast into a
 *    struct scic_sds_controller object.
 * @remote_device: This is struct sci_base_remote_device which is cast to a
 *    struct scic_sds_remote_device object.
 * @io_request: This is the struct sci_base_request which is cast to a
 *    SCIC_SDS_IO_REQUEST object.
 *
 * This method is called when the struct scic_sds_controller is in a stopping state
 * and the complete task handler is called. - This function is not yet
 * implemented enum sci_status SCI_FAILURE
 */

/*
 * *****************************************************************************
 * * STOPPED STATE HANDLERS
 * ***************************************************************************** */

/*
 * *****************************************************************************
 * * FAILED STATE HANDLERS
 * ***************************************************************************** */

const struct scic_sds_controller_state_handler scic_sds_controller_state_handler_table[] = {
	[SCI_BASE_CONTROLLER_STATE_INITIAL] = {
		.base.start_io     = scic_sds_controller_default_start_operation_handler,
		.base.complete_io  = scic_sds_controller_default_request_handler,
		.base.continue_io  = scic_sds_controller_default_request_handler,
		.terminate_request = scic_sds_controller_default_request_handler,
	},
	[SCI_BASE_CONTROLLER_STATE_RESET] = {
		.base.initialize   = scic_sds_controller_reset_state_initialize_handler,
		.base.start_io     = scic_sds_controller_default_start_operation_handler,
		.base.complete_io  = scic_sds_controller_default_request_handler,
		.base.continue_io  = scic_sds_controller_default_request_handler,
		.terminate_request = scic_sds_controller_default_request_handler,
	},
	[SCI_BASE_CONTROLLER_STATE_INITIALIZING] = {
		.base.start_io     = scic_sds_controller_default_start_operation_handler,
		.base.complete_io  = scic_sds_controller_default_request_handler,
		.base.continue_io  = scic_sds_controller_default_request_handler,
		.terminate_request = scic_sds_controller_default_request_handler,
	},
	[SCI_BASE_CONTROLLER_STATE_INITIALIZED] = {
		.base.start        = scic_sds_controller_initialized_state_start_handler,
		.base.start_io     = scic_sds_controller_default_start_operation_handler,
		.base.complete_io  = scic_sds_controller_default_request_handler,
		.base.continue_io  = scic_sds_controller_default_request_handler,
		.terminate_request = scic_sds_controller_default_request_handler,
	},
	[SCI_BASE_CONTROLLER_STATE_STARTING] = {
		.base.start_io     = scic_sds_controller_default_start_operation_handler,
		.base.complete_io  = scic_sds_controller_default_request_handler,
		.base.continue_io  = scic_sds_controller_default_request_handler,
		.terminate_request = scic_sds_controller_default_request_handler,
		.link_up           = scic_sds_controller_starting_state_link_up_handler,
		.link_down	   = scic_sds_controller_starting_state_link_down_handler
	},
	[SCI_BASE_CONTROLLER_STATE_READY] = {
		.base.stop         = scic_sds_controller_ready_state_stop_handler,
		.base.reset        = scic_sds_controller_general_reset_handler,
		.base.start_io     = scic_sds_controller_ready_state_start_io_handler,
		.base.complete_io  = scic_sds_controller_ready_state_complete_io_handler,
		.base.continue_io  = scic_sds_controller_ready_state_continue_io_handler,
		.base.start_task   = scic_sds_controller_ready_state_start_task_handler,
		.base.complete_task = scic_sds_controller_ready_state_complete_io_handler,
		.terminate_request = scic_sds_controller_ready_state_terminate_request_handler,
		.link_up           = scic_sds_controller_ready_state_link_up_handler,
		.link_down	   = scic_sds_controller_ready_state_link_down_handler
	},
	[SCI_BASE_CONTROLLER_STATE_RESETTING] = {
		.base.start_io     = scic_sds_controller_default_start_operation_handler,
		.base.complete_io  = scic_sds_controller_default_request_handler,
		.base.continue_io  = scic_sds_controller_default_request_handler,
		.terminate_request = scic_sds_controller_default_request_handler,
	},
	[SCI_BASE_CONTROLLER_STATE_STOPPING] = {
		.base.start_io     = scic_sds_controller_default_start_operation_handler,
		.base.complete_io  = scic_sds_controller_stopping_state_complete_io_handler,
		.base.continue_io  = scic_sds_controller_default_request_handler,
		.terminate_request = scic_sds_controller_default_request_handler,
	},
	[SCI_BASE_CONTROLLER_STATE_STOPPED] = {
		.base.reset        = scic_sds_controller_general_reset_handler,
		.base.start_io     = scic_sds_controller_default_start_operation_handler,
		.base.complete_io  = scic_sds_controller_default_request_handler,
		.base.continue_io  = scic_sds_controller_default_request_handler,
		.terminate_request = scic_sds_controller_default_request_handler,
	},
	[SCI_BASE_CONTROLLER_STATE_FAILED] = {
		.base.reset        = scic_sds_controller_general_reset_handler,
		.base.start_io     = scic_sds_controller_default_start_operation_handler,
		.base.complete_io  = scic_sds_controller_default_request_handler,
		.base.continue_io  = scic_sds_controller_default_request_handler,
		.terminate_request = scic_sds_controller_default_request_handler,
	},
};

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_controller
 *    object.
 *
 * This method implements the actions taken by the struct scic_sds_controller on entry
 * to the SCI_BASE_CONTROLLER_STATE_INITIAL. - Set the state handlers to the
 * controllers initial state. none This function should initialze the
 * controller object.
 */
static void scic_sds_controller_initial_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_controller *this_controller;

	this_controller = (struct scic_sds_controller *)object;

	sci_base_state_machine_change_state(
		&this_controller->parent.state_machine, SCI_BASE_CONTROLLER_STATE_RESET);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_controller
 *    object.
 *
 * This method implements the actions taken by the struct scic_sds_controller on exit
 * from the SCI_BASE_CONTROLLER_STATE_STARTING. - This function stops the
 * controller starting timeout timer. none
 */
static void scic_sds_controller_starting_state_exit(
	struct sci_base_object *object)
{
	struct scic_sds_controller *scic = (struct scic_sds_controller *)object;

	isci_event_timer_stop(scic, scic->timeout_timer);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_controller
 *    object.
 *
 * This method implements the actions taken by the struct scic_sds_controller on entry
 * to the SCI_BASE_CONTROLLER_STATE_READY. - Set the state handlers to the
 * controllers ready state. none
 */
static void scic_sds_controller_ready_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_controller *this_controller;

	this_controller = (struct scic_sds_controller *)object;

	/* set the default interrupt coalescence number and timeout value. */
	scic_controller_set_interrupt_coalescence(
		this_controller, 0x10, 250);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_controller
 *    object.
 *
 * This method implements the actions taken by the struct scic_sds_controller on exit
 * from the SCI_BASE_CONTROLLER_STATE_READY. - This function does nothing. none
 */
static void scic_sds_controller_ready_state_exit(
	struct sci_base_object *object)
{
	struct scic_sds_controller *this_controller;

	this_controller = (struct scic_sds_controller *)object;

	/* disable interrupt coalescence. */
	scic_controller_set_interrupt_coalescence(this_controller, 0, 0);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_controller
 *    object.
 *
 * This method implements the actions taken by the struct scic_sds_controller on entry
 * to the SCI_BASE_CONTROLLER_STATE_READY. - Set the state handlers to the
 * controllers ready state. - Stop the phys on this controller - Stop the ports
 * on this controller - Stop all of the remote devices on this controller none
 */
static void scic_sds_controller_stopping_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_controller *this_controller;

	this_controller = (struct scic_sds_controller *)object;

	/* Stop all of the components for this controller */
	scic_sds_controller_stop_phys(this_controller);
	scic_sds_controller_stop_ports(this_controller);
	scic_sds_controller_stop_devices(this_controller);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_controller
 *    object.
 *
 * This method implements the actions taken by the struct scic_sds_controller on exit
 * from the SCI_BASE_CONTROLLER_STATE_STOPPING. - This function stops the
 * controller stopping timeout timer. none
 */
static void scic_sds_controller_stopping_state_exit(
	struct sci_base_object *object)
{
	struct scic_sds_controller *this_controller;

	this_controller = (struct scic_sds_controller *)object;

	isci_event_timer_stop(this_controller, this_controller->timeout_timer);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_controller
 *    object.
 *
 * This method implements the actions taken by the struct scic_sds_controller on entry
 * to the SCI_BASE_CONTROLLER_STATE_RESETTING. - Set the state handlers to the
 * controllers resetting state. - Write to the SCU hardware reset register to
 * force a reset - Transition to the SCI_BASE_CONTROLLER_STATE_RESET none
 */
static void scic_sds_controller_resetting_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_controller *this_controller;

	this_controller = (struct scic_sds_controller *)object;

	scic_sds_controller_reset_hardware(this_controller);

	sci_base_state_machine_change_state(
		scic_sds_controller_get_base_state_machine(this_controller),
		SCI_BASE_CONTROLLER_STATE_RESET
		);
}

/* --------------------------------------------------------------------------- */

const struct sci_base_state scic_sds_controller_state_table[] = {
	[SCI_BASE_CONTROLLER_STATE_INITIAL] = {
		.enter_state = scic_sds_controller_initial_state_enter,
	},
	[SCI_BASE_CONTROLLER_STATE_RESET] = {},
	[SCI_BASE_CONTROLLER_STATE_INITIALIZING] = {},
	[SCI_BASE_CONTROLLER_STATE_INITIALIZED] = {},
	[SCI_BASE_CONTROLLER_STATE_STARTING] = {
		.exit_state  = scic_sds_controller_starting_state_exit,
	},
	[SCI_BASE_CONTROLLER_STATE_READY] = {
		.enter_state = scic_sds_controller_ready_state_enter,
		.exit_state  = scic_sds_controller_ready_state_exit,
	},
	[SCI_BASE_CONTROLLER_STATE_RESETTING] = {
		.enter_state = scic_sds_controller_resetting_state_enter,
	},
	[SCI_BASE_CONTROLLER_STATE_STOPPING] = {
		.enter_state = scic_sds_controller_stopping_state_enter,
		.exit_state = scic_sds_controller_stopping_state_exit,
	},
	[SCI_BASE_CONTROLLER_STATE_STOPPED] = {},
	[SCI_BASE_CONTROLLER_STATE_FAILED] = {}
};

