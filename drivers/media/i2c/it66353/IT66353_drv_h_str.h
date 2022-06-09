// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 *
 * it66353 HDMI 3 in 1 out driver.
 *
 * Author: Kenneth.Hung@ite.com.tw
 * 	   Wangqiang Guo <kay.guo@rock-chips.com>
 * Version: IT66353_SAMPLE_1.08
 *
 */
#ifdef __init_str_SYS_FSM_STATE
static char *s__SYS_FSM_STATE[]	= {
	"RX_TOGGLE_HPD",
	"RX_PORT_CHANGE",
	"TX_OUTPUT",
	"TX_OUTPUT_PREPARE",
	"RX_CHECK_EQ",
	"SETUP_AFE",
	"RX_WAIT_CLOCK",
	"RX_HPD",
	"TX_GOT_HPD",
	"TX_WAIT_HPD",
	"TX_UNPLUG",
	"RX_UNPLUG",
	"IDLE",
};

#endif
#ifdef __init_str_DEV_FSM_STATE
static char *s__DEV_FSM_STATE[] = {
	"DEV_DEVICE_LOOP",
	"DEV_DEVICE_INIT",
	"DEV_WAIT_DEVICE_READY",
	"DEV_FW_VAR_INIT",
	"DEV_WAIT_RESET",
};

#endif
#ifdef __init_str_AEQ_FSM_STATE
static char *s__AEQ_FSM_STATE[] = {
	"AEQ_OFF",
	"AEQ_START",
	"AEQ_CHECK_SAREQ_RESULT",
	"AEQ_APPLY_SAREQ",
	"AEQ_DONE",
	"AEQ_FAIL",
	"AEQ_MAX",
};

#endif
#ifdef __init_str_EQ_RESULT_TYPE
static char *s__EQ_RESULT_TYPE[] = {
	"EQRES_UNKNOWN",
	"EQRES_BUSY",
	"EQRES_SAREQ_DONE",
	"EQRES_SAREQ_FAIL",
	"EQRES_SAREQ_TIMEOUT",
	"EQRES_H14EQ_DONE",
	"EQRES_H14EQ_FAIL",
	"EQRES_H14EQ_TIMEOUT",
	"EQRES_DONE",
};

#endif
#ifdef __init_str_SYS_AEQ_TYPE
static char *s__SYS_AEQ_TYPE[] = {
	"SysAEQ_OFF",
	"SysAEQ_RUN",
	"SysAEQ_DONE",
};

#endif
