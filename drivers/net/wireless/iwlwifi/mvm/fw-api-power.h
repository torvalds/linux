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
 * enum iwl_ltr_config_flags - masks for LTR config command flags
 * @LTR_CFG_FLAG_FEATURE_ENABLE: Feature operational status
 * @LTR_CFG_FLAG_HW_DIS_ON_SHADOW_REG_ACCESS: allow LTR change on shadow
 *	memory access
 * @LTR_CFG_FLAG_HW_EN_SHRT_WR_THROUGH: allow LTR msg send on ANY LTR
 *	reg change
 * @LTR_CFG_FLAG_HW_DIS_ON_D0_2_D3: allow LTR msg send on transition from
 *	D0 to D3
 * @LTR_CFG_FLAG_SW_SET_SHORT: fixed static short LTR register
 * @LTR_CFG_FLAG_SW_SET_LONG: fixed static short LONG register
 * @LTR_CFG_FLAG_DENIE_C10_ON_PD: allow going into C10 on PD
 */
enum iwl_ltr_config_flags {
	LTR_CFG_FLAG_FEATURE_ENABLE = BIT(0),
	LTR_CFG_FLAG_HW_DIS_ON_SHADOW_REG_ACCESS = BIT(1),
	LTR_CFG_FLAG_HW_EN_SHRT_WR_THROUGH = BIT(2),
	LTR_CFG_FLAG_HW_DIS_ON_D0_2_D3 = BIT(3),
	LTR_CFG_FLAG_SW_SET_SHORT = BIT(4),
	LTR_CFG_FLAG_SW_SET_LONG = BIT(5),
	LTR_CFG_FLAG_DENIE_C10_ON_PD = BIT(6),
};

/**
 * struct iwl_ltr_config_cmd - configures the LTR
 * @flags: See %enum iwl_ltr_config_flags
 */
struct iwl_ltr_config_cmd {
	__le32 flags;
	__le32 static_long;
	__le32 static_short;
} __packed;

/**
 * enum iwl_power_flags - masks for power table command flags
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
 * @keep_alive_beacons:	not in use
 * @lprx_rssi_threshold: Signal strength up to which LP RX can be enabled.
 *			Default: 80dbm
 */
struct iwl_powertable_cmd {
	/* PM_POWER_TABLE_CMD_API_S_VER_5 */
	__le16 flags;
	u8 keep_alive_seconds;
	u8 debug_flags;
	__le32 rx_data_timeout;
	__le32 tx_data_timeout;
	__le32 sleep_interval[IWL_POWER_VEC_SIZE];
	__le32 keep_alive_beacons;
	__le32 lprx_rssi_threshold;
} __packed;

#endif
