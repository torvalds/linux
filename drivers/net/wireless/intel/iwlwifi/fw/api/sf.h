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

#ifndef __iwl_fw_api_sf_h__
#define __iwl_fw_api_sf_h__

/* Smart Fifo state */
enum iwl_sf_state {
	SF_LONG_DELAY_ON = 0, /* should never be called by driver */
	SF_FULL_ON,
	SF_UNINIT,
	SF_INIT_OFF,
	SF_HW_NUM_STATES
};

/* Smart Fifo possible scenario */
enum iwl_sf_scenario {
	SF_SCENARIO_SINGLE_UNICAST,
	SF_SCENARIO_AGG_UNICAST,
	SF_SCENARIO_MULTICAST,
	SF_SCENARIO_BA_RESP,
	SF_SCENARIO_TX_RESP,
	SF_NUM_SCENARIO
};

#define SF_TRANSIENT_STATES_NUMBER 2	/* SF_LONG_DELAY_ON and SF_FULL_ON */
#define SF_NUM_TIMEOUT_TYPES 2		/* Aging timer and Idle timer */

/* smart FIFO default values */
#define SF_W_MARK_SISO 6144
#define SF_W_MARK_MIMO2 8192
#define SF_W_MARK_MIMO3 6144
#define SF_W_MARK_LEGACY 4096
#define SF_W_MARK_SCAN 4096

/* SF Scenarios timers for default configuration (aligned to 32 uSec) */
#define SF_SINGLE_UNICAST_IDLE_TIMER_DEF 160	/* 150 uSec  */
#define SF_SINGLE_UNICAST_AGING_TIMER_DEF 400	/* 0.4 mSec */
#define SF_AGG_UNICAST_IDLE_TIMER_DEF 160		/* 150 uSec */
#define SF_AGG_UNICAST_AGING_TIMER_DEF 400		/* 0.4 mSec */
#define SF_MCAST_IDLE_TIMER_DEF 160		/* 150 mSec */
#define SF_MCAST_AGING_TIMER_DEF 400		/* 0.4 mSec */
#define SF_BA_IDLE_TIMER_DEF 160			/* 150 uSec */
#define SF_BA_AGING_TIMER_DEF 400			/* 0.4 mSec */
#define SF_TX_RE_IDLE_TIMER_DEF 160			/* 150 uSec */
#define SF_TX_RE_AGING_TIMER_DEF 400		/* 0.4 mSec */

/* SF Scenarios timers for BSS MAC configuration (aligned to 32 uSec) */
#define SF_SINGLE_UNICAST_IDLE_TIMER 320	/* 300 uSec  */
#define SF_SINGLE_UNICAST_AGING_TIMER 2016	/* 2 mSec */
#define SF_AGG_UNICAST_IDLE_TIMER 320		/* 300 uSec */
#define SF_AGG_UNICAST_AGING_TIMER 2016		/* 2 mSec */
#define SF_MCAST_IDLE_TIMER 2016		/* 2 mSec */
#define SF_MCAST_AGING_TIMER 10016		/* 10 mSec */
#define SF_BA_IDLE_TIMER 320			/* 300 uSec */
#define SF_BA_AGING_TIMER 2016			/* 2 mSec */
#define SF_TX_RE_IDLE_TIMER 320			/* 300 uSec */
#define SF_TX_RE_AGING_TIMER 2016		/* 2 mSec */

#define SF_LONG_DELAY_AGING_TIMER 1000000	/* 1 Sec */

#define SF_CFG_DUMMY_NOTIF_OFF	BIT(16)

/**
 * struct iwl_sf_cfg_cmd - Smart Fifo configuration command.
 * @state: smart fifo state, types listed in &enum iwl_sf_state.
 * @watermark: Minimum allowed available free space in RXF for transient state.
 * @long_delay_timeouts: aging and idle timer values for each scenario
 * in long delay state.
 * @full_on_timeouts: timer values for each scenario in full on state.
 */
struct iwl_sf_cfg_cmd {
	__le32 state;
	__le32 watermark[SF_TRANSIENT_STATES_NUMBER];
	__le32 long_delay_timeouts[SF_NUM_SCENARIO][SF_NUM_TIMEOUT_TYPES];
	__le32 full_on_timeouts[SF_NUM_SCENARIO][SF_NUM_TIMEOUT_TYPES];
} __packed; /* SF_CFG_API_S_VER_2 */

#endif /* __iwl_fw_api_sf_h__ */
