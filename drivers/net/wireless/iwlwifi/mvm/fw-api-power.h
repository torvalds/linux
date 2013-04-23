/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
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

#ifndef __fw_api_power_h__
#define __fw_api_power_h__

/* Power Management Commands, Responses, Notifications */

/**
 * enum iwl_scan_flags - masks for power table command flags
 * @POWER_FLAGS_POWER_SAVE_ENA_MSK: '1' Allow to save power by turning off
 *		receiver and transmitter. '0' - does not allow.
 * @POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK: '0' Driver disables power management,
 *		'1' Driver enables PM (use rest of parameters)
 * @POWER_FLAGS_SKIP_OVER_DTIM_MSK: '0' PM have to walk up every DTIM,
 *		'1' PM could sleep over DTIM till listen Interval.
 * @POWER_FLAGS_ADVANCE_PM_ENA_MSK: Advanced PM (uAPSD) enable mask
 * @POWER_FLAGS_LPRX_ENA_MSK: Low Power RX enable.
*/
enum iwl_power_flags {
	POWER_FLAGS_POWER_SAVE_ENA_MSK		= BIT(0),
	POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK	= BIT(1),
	POWER_FLAGS_SKIP_OVER_DTIM_MSK		= BIT(2),
	POWER_FLAGS_ADVANCE_PM_ENA_MSK		= BIT(9),
	POWER_FLAGS_LPRX_ENA_MSK		= BIT(11),
};

#define IWL_POWER_VEC_SIZE 5

/**
 * struct iwl_powertable_cmd - Power Table Command
 * POWER_TABLE_CMD = 0x77 (command, has simple generic response)
 *
 * @flags:		Power table command flags from POWER_FLAGS_*
 * @keep_alive_seconds: Keep alive period in seconds. Default - 25 sec.
 *			Minimum allowed:- 3 * DTIM. Keep alive period must be
 *			set regardless of power scheme or current power state.
 *			FW use this value also when PM is disabled.
 * @rx_data_timeout:    Minimum time (usec) from last Rx packet for AM to
 *			PSM transition - legacy PM
 * @tx_data_timeout:    Minimum time (usec) from last Tx packet for AM to
 *			PSM transition - legacy PM
 * @sleep_interval:	not in use
 * @num_skip_dtim:	Number of DTIMs to skip if Skip over DTIM flag is set
 * @lprx_rssi_threshold: Signal strength up to which LP RX can be enabled.
 *			Default: 80dbm
 */
struct iwl_powertable_cmd {
	/* PM_POWER_TABLE_CMD_API_S_VER_6 */
	__le16 flags;
	u8 keep_alive_seconds;
	u8 debug_flags;
	__le32 rx_data_timeout;
	__le32 tx_data_timeout;
	__le32 sleep_interval[IWL_POWER_VEC_SIZE];
	__le32 num_skip_dtim;
	__le32 lprx_rssi_threshold;
} __packed;

/**
 * struct iwl_beacon_filter_cmd
 * REPLY_BEACON_FILTERING_CMD = 0xd2 (command)
 * @id_and_color: MAC contex identifier
 * @bf_energy_delta: Used for RSSI filtering, if in 'normal' state. Send beacon
 *      to driver if delta in Energy values calculated for this and last
 *      passed beacon is greater than this threshold. Zero value means that
 *      the Energy change is ignored for beacon filtering, and beacon will
 *      not be forced to be sent to driver regardless of this delta. Typical
 *      energy delta 5dB.
 * @bf_roaming_energy_delta: Used for RSSI filtering, if in 'roaming' state.
 *      Send beacon to driver if delta in Energy values calculated for this
 *      and last passed beacon is greater than this threshold. Zero value
 *      means that the Energy change is ignored for beacon filtering while in
 *      Roaming state, typical energy delta 1dB.
 * @bf_roaming_state: Used for RSSI filtering. If absolute Energy values
 *      calculated for current beacon is less than the threshold, use
 *      Roaming Energy Delta Threshold, otherwise use normal Energy Delta
 *      Threshold. Typical energy threshold is -72dBm.
 * @bf_temperature_delta: Send Beacon to driver if delta in temperature values
 *      calculated for this and the last passed beacon is greater than  this
 *      threshold. Zero value means that the temperature changeis ignored for
 *      beacon filtering; beacons will not be  forced to be sent to driver
 *      regardless of whether its temerature has been changed.
 * @bf_enable_beacon_filter: 1, beacon filtering is enabled; 0, disabled.
 * @bf_filter_escape_timer: Send beacons to to driver if no beacons were passed
 *      for a specific period of time. Units: Beacons.
 * @ba_escape_timer: Fully receive and parse beacon if no beacons were passed
 *      for a longer period of time then this escape-timeout. Units: Beacons.
 * @ba_enable_beacon_abort: 1, beacon abort is enabled; 0, disabled.
 */
struct iwl_beacon_filter_cmd {
	u8 bf_energy_delta;
	u8 bf_roaming_energy_delta;
	u8 bf_roaming_state;
	u8 bf_temperature_delta;
	u8 bf_enable_beacon_filter;
	u8 bf_debug_flag;
	__le16 reserved1;
	__le32 bf_escape_timer;
	__le32 ba_escape_timer;
	u8 ba_enable_beacon_abort;
	u8 reserved2[3];
} __packed;

/* Beacon filtering and beacon abort */
#define IWL_BF_ENERGY_DELTA_DEFAULT 5
#define IWL_BF_ENERGY_DELTA_MAX 255
#define IWL_BF_ENERGY_DELTA_MIN 0

#define IWL_BF_ROAMING_ENERGY_DELTA_DEFAULT 1
#define IWL_BF_ROAMING_ENERGY_DELTA_MAX 255
#define IWL_BF_ROAMING_ENERGY_DELTA_MIN 0

#define IWL_BF_ROAMING_STATE_DEFAULT 72
#define IWL_BF_ROAMING_STATE_MAX 255
#define IWL_BF_ROAMING_STATE_MIN 0

#define IWL_BF_TEMPERATURE_DELTA_DEFAULT 5
#define IWL_BF_TEMPERATURE_DELTA_MAX 255
#define IWL_BF_TEMPERATURE_DELTA_MIN 0

#define IWL_BF_ENABLE_BEACON_FILTER_DEFAULT 1

#define IWL_BF_DEBUG_FLAG_DEFAULT 0

#define IWL_BF_ESCAPE_TIMER_DEFAULT 50
#define IWL_BF_ESCAPE_TIMER_MAX 1024
#define IWL_BF_ESCAPE_TIMER_MIN 0

#define IWL_BA_ESCAPE_TIMER_DEFAULT 3
#define IWL_BA_ESCAPE_TIMER_MAX 1024
#define IWL_BA_ESCAPE_TIMER_MIN 0

#define IWL_BA_ENABLE_BEACON_ABORT_DEFAULT 1

#endif
