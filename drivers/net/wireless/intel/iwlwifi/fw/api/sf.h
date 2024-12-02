/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2012-2014 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
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
