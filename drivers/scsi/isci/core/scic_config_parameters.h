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

#ifndef _SCIC_SDS_USER_PARAMETERS_H_
#define _SCIC_SDS_USER_PARAMETERS_H_

/**
 * This file contains all of the structure definitions and interface methods
 *    that can be called by a SCIC user on the SCU Driver Standard
 *    (struct scic_sds_user_parameters) user parameter block.
 *
 *
 */


#include "sci_status.h"
#include "intel_sas.h"
#include "sci_controller_constants.h"
#include "probe_roms.h"

struct scic_sds_controller;

/**
 *
 *
 * SCIC_SDS_PARM_PHY_SPEED These constants define the speeds utilized for a
 * phy/port.
 */
#define SCIC_SDS_PARM_NO_SPEED   0

/**
 *
 *
 * This value of 1 indicates generation 1 (i.e. 1.5 Gb/s).
 */
#define SCIC_SDS_PARM_GEN1_SPEED 1

/**
 *
 *
 * This value of 2 indicates generation 2 (i.e. 3.0 Gb/s).
 */
#define SCIC_SDS_PARM_GEN2_SPEED 2

/**
 *
 *
 * This value of 3 indicates generation 3 (i.e. 6.0 Gb/s).
 */
#define SCIC_SDS_PARM_GEN3_SPEED 3

/**
 *
 *
 * For range checks, the max speed generation
 */
#define SCIC_SDS_PARM_MAX_SPEED SCIC_SDS_PARM_GEN3_SPEED

/**
 * struct scic_sds_user_parameters - This structure delineates the various user
 *    parameters that can be changed by the core user.
 *
 *
 */
struct scic_sds_user_parameters {
	struct sci_phy_user_params {
		/**
		 * This field specifies the NOTIFY (ENABLE SPIN UP) primitive
		 * insertion frequency for this phy index.
		 */
		u32 notify_enable_spin_up_insertion_frequency;

		/**
		 * This method specifies the number of transmitted DWORDs within which
		 * to transmit a single ALIGN primitive.  This value applies regardless
		 * of what type of device is attached or connection state.  A value of
		 * 0 indicates that no ALIGN primitives will be inserted.
		 */
		u16 align_insertion_frequency;

		/**
		 * This method specifies the number of transmitted DWORDs within which
		 * to transmit 2 ALIGN primitives.  This applies for SAS connections
		 * only.  A minimum value of 3 is required for this field.
		 */
		u16 in_connection_align_insertion_frequency;

		/**
		 * This field indicates the maximum speed generation to be utilized
		 * by phys in the supplied port.
		 * - A value of 1 indicates generation 1 (i.e. 1.5 Gb/s).
		 * - A value of 2 indicates generation 2 (i.e. 3.0 Gb/s).
		 * - A value of 3 indicates generation 3 (i.e. 6.0 Gb/s).
		 */
		u8 max_speed_generation;

	} phys[SCI_MAX_PHYS];

	/**
	 * This field specifies the maximum number of direct attached devices
	 * that can have power supplied to them simultaneously.
	 */
	u8 max_number_concurrent_device_spin_up;

	/**
	 * This field specifies the number of seconds to allow a phy to consume
	 * power before yielding to another phy.
	 *
	 */
	u8 phy_spin_up_delay_interval;

	/**
	 * These timer values specifies how long a link will remain open with no
	 * activity in increments of a microsecond, it can be in increments of
	 * 100 microseconds if the upper most bit is set.
	 *
	 */
	u16 stp_inactivity_timeout;
	u16 ssp_inactivity_timeout;

	/**
	 * These timer values specifies how long a link will remain open in increments
	 * of 100 microseconds.
	 *
	 */
	u16 stp_max_occupancy_timeout;
	u16 ssp_max_occupancy_timeout;

	/**
	 * This timer value specifies how long a link will remain open with no
	 * outbound traffic in increments of a microsecond.
	 *
	 */
	u8 no_outbound_task_timeout;

};

/**
 * This structure/union specifies the various different user parameter sets
 *    available.  Each type is specific to a hardware controller version.
 *
 * union scic_user_parameters
 */
union scic_user_parameters {
	/**
	 * This field specifies the user parameters specific to the
	 * Storage Controller Unit (SCU) Driver Standard (SDS) version
	 * 1.
	 */
	struct scic_sds_user_parameters sds1;

};


/**
 *
 *
 * SCIC_SDS_OEM_PHY_MASK These constants define the valid values for phy_mask
 */

/**
 *
 *
 * This is the min value assignable to a port's phy mask
 */
#define SCIC_SDS_PARM_PHY_MASK_MIN 0x0

/**
 *
 *
 * This is the max value assignable to a port's phy mask
 */
#define SCIC_SDS_PARM_PHY_MASK_MAX 0xF

#define MAX_CONCURRENT_DEVICE_SPIN_UP_COUNT 4

/**
 * This structure/union specifies the various different OEM parameter sets
 *    available.  Each type is specific to a hardware controller version.
 *
 * union scic_oem_parameters
 */
union scic_oem_parameters {
	/**
	 * This field specifies the OEM parameters specific to the
	 * Storage Controller Unit (SCU) Driver Standard (SDS) version
	 * 1.
	 */
	struct scic_sds_oem_params sds1;
};

/**
 * scic_user_parameters_set() - This method allows the user to attempt to
 *    change the user parameters utilized by the controller.
 * @controller: This parameter specifies the controller on which to set the
 *    user parameters.
 * @user_parameters: This parameter specifies the USER_PARAMETERS object
 *    containing the potential new values.
 *
 * Indicate if the update of the user parameters was successful. SCI_SUCCESS
 * This value is returned if the operation succeeded. SCI_FAILURE_INVALID_STATE
 * This value is returned if the attempt to change the user parameter failed,
 * because changing one of the parameters is not currently allowed.
 * SCI_FAILURE_INVALID_PARAMETER_VALUE This value is returned if the user
 * supplied an invalid interrupt coalescence time, spin up delay interval, etc.
 */
enum sci_status scic_user_parameters_set(
	struct scic_sds_controller *controller,
	union scic_user_parameters *user_parameters);

/**
 * scic_oem_parameters_set() - This method allows the user to attempt to change
 *    the OEM parameters utilized by the controller.
 * @controller: This parameter specifies the controller on which to set the
 *    user parameters.
 * @oem_parameters: This parameter specifies the OEM parameters object
 *    containing the potential new values.
 *
 * Indicate if the update of the user parameters was successful. SCI_SUCCESS
 * This value is returned if the operation succeeded. SCI_FAILURE_INVALID_STATE
 * This value is returned if the attempt to change the user parameter failed,
 * because changing one of the parameters is not currently allowed.
 * SCI_FAILURE_INVALID_PARAMETER_VALUE This value is returned if the user
 * supplied an unsupported value for one of the OEM parameters.
 */
enum sci_status scic_oem_parameters_set(
	struct scic_sds_controller *controller,
	union scic_oem_parameters *oem_parameters);

int scic_oem_parameters_validate(struct scic_sds_oem_params *oem);

/**
 * scic_oem_parameters_get() - This method allows the user to retreive the OEM
 *    parameters utilized by the controller.
 * @controller: This parameter specifies the controller on which to set the
 *    user parameters.
 * @oem_parameters: This parameter specifies the OEM parameters object in which
 *    to write the core's OEM parameters.
 *
 */
void scic_oem_parameters_get(
	struct scic_sds_controller *controller,
	union scic_oem_parameters *oem_parameters);


#endif  /* _SCIC_SDS_USER_PARAMETERS_H_ */

