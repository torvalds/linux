/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
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
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
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
 *
 *****************************************************************************/

#ifndef __iwl_fw_api_mac_cfg_h__
#define __iwl_fw_api_mac_cfg_h__

/**
 * enum iwl_mac_conf_subcmd_ids - mac configuration command IDs
 */
enum iwl_mac_conf_subcmd_ids {
	/**
	 * @LINK_QUALITY_MEASUREMENT_CMD: &struct iwl_link_qual_msrmnt_cmd
	 */
	LINK_QUALITY_MEASUREMENT_CMD = 0x1,

	/**
	 * @LINK_QUALITY_MEASUREMENT_COMPLETE_NOTIF:
	 * &struct iwl_link_qual_msrmnt_notif
	 */
	LINK_QUALITY_MEASUREMENT_COMPLETE_NOTIF = 0xFE,

	/**
	 * @CHANNEL_SWITCH_NOA_NOTIF: &struct iwl_channel_switch_noa_notif
	 */
	CHANNEL_SWITCH_NOA_NOTIF = 0xFF,
};

#define LQM_NUMBER_OF_STATIONS_IN_REPORT 16

enum iwl_lqm_cmd_operatrions {
	LQM_CMD_OPERATION_START_MEASUREMENT = 0x01,
	LQM_CMD_OPERATION_STOP_MEASUREMENT = 0x02,
};

enum iwl_lqm_status {
	LQM_STATUS_SUCCESS = 0,
	LQM_STATUS_TIMEOUT = 1,
	LQM_STATUS_ABORT = 2,
};

/**
 * struct iwl_link_qual_msrmnt_cmd - Link Quality Measurement command
 * @cmd_operation: command operation to be performed (start or stop)
 *	as defined above.
 * @mac_id: MAC ID the measurement applies to.
 * @measurement_time: time of the total measurement to be performed, in uSec.
 * @timeout: maximum time allowed until a response is sent, in uSec.
 */
struct iwl_link_qual_msrmnt_cmd {
	__le32 cmd_operation;
	__le32 mac_id;
	__le32 measurement_time;
	__le32 timeout;
} __packed /* LQM_CMD_API_S_VER_1 */;

/**
 * struct iwl_link_qual_msrmnt_notif - Link Quality Measurement notification
 *
 * @frequent_stations_air_time: an array containing the total air time
 *	(in uSec) used by the most frequently transmitting stations.
 * @number_of_stations: the number of uniqe stations included in the array
 *	(a number between 0 to 16)
 * @total_air_time_other_stations: the total air time (uSec) used by all the
 *	stations which are not included in the above report.
 * @time_in_measurement_window: the total time in uSec in which a measurement
 *	took place.
 * @tx_frame_dropped: the number of TX frames dropped due to retry limit during
 *	measurement
 * @mac_id: MAC ID the measurement applies to.
 * @status: return status. may be one of the LQM_STATUS_* defined above.
 * @reserved: reserved.
 */
struct iwl_link_qual_msrmnt_notif {
	__le32 frequent_stations_air_time[LQM_NUMBER_OF_STATIONS_IN_REPORT];
	__le32 number_of_stations;
	__le32 total_air_time_other_stations;
	__le32 time_in_measurement_window;
	__le32 tx_frame_dropped;
	__le32 mac_id;
	__le32 status;
	u8 reserved[12];
} __packed; /* LQM_MEASUREMENT_COMPLETE_NTF_API_S_VER1 */

/**
 * struct iwl_channel_switch_noa_notif - Channel switch NOA notification
 *
 * @id_and_color: ID and color of the MAC
 */
struct iwl_channel_switch_noa_notif {
	__le32 id_and_color;
} __packed; /* CHANNEL_SWITCH_START_NTFY_API_S_VER_1 */

#endif /* __iwl_fw_api_mac_cfg_h__ */
