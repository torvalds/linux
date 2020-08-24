/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2017 - 2019 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#ifndef _HALMAC_STATE_MACHINE_H_
#define _HALMAC_STATE_MACHINE_H_

enum halmac_dlfw_state {
	HALMAC_DLFW_NONE = 0,
	HALMAC_DLFW_DONE = 1,
	HALMAC_GEN_INFO_SENT = 2,

	/* Data CPU firmware download framework */
	HALMAC_DLFW_INIT = 0x11,
	HALMAC_DLFW_START = 0x12,
	HALMAC_DLFW_CONF_READY = 0x13,
	HALMAC_DLFW_CPU_READY = 0x14,
	HALMAC_DLFW_MEM_READY = 0x15,
	HALMAC_DLFW_SW_READY = 0x16,
	HALMAC_DLFW_OFLD_READY = 0x17,

	HALMAC_DLFW_UNDEFINED = 0x7F,
};

enum halmac_gpio_cfg_state {
	HALMAC_GPIO_CFG_STATE_IDLE = 0,
	HALMAC_GPIO_CFG_STATE_BUSY = 1,
	HALMAC_GPIO_CFG_STATE_UNDEFINED = 0x7F,
};

enum halmac_rsvd_pg_state {
	HALMAC_RSVD_PG_STATE_IDLE = 0,
	HALMAC_RSVD_PG_STATE_BUSY = 1,
	HALMAC_RSVD_PG_STATE_UNDEFINED = 0x7F,
};

enum halmac_api_state {
	HALMAC_API_STATE_INIT = 0,
	HALMAC_API_STATE_HALT = 1,
	HALMAC_API_STATE_UNDEFINED = 0x7F,
};

enum halmac_cmd_construct_state {
	HALMAC_CMD_CNSTR_IDLE = 0,
	HALMAC_CMD_CNSTR_BUSY = 1,
	HALMAC_CMD_CNSTR_H2C_SENT = 2,
	HALMAC_CMD_CNSTR_CNSTR = 3,
	HALMAC_CMD_CNSTR_BUF_CLR = 4,
	HALMAC_CMD_CNSTR_UNDEFINED = 0x7F,
};

enum halmac_cmd_process_status {
	HALMAC_CMD_PROCESS_IDLE = 0x01, /* Init status */
	HALMAC_CMD_PROCESS_SENDING = 0x02, /* Wait ack */
	HALMAC_CMD_PROCESS_RCVD = 0x03, /* Rcvd ack */
	HALMAC_CMD_PROCESS_DONE = 0x04, /* Event done */
	HALMAC_CMD_PROCESS_ERROR = 0x05, /* Return code error */
	HALMAC_CMD_PROCESS_UNDEFINE = 0x7F,
};

enum halmac_mac_power {
	HALMAC_MAC_POWER_OFF = 0x0,
	HALMAC_MAC_POWER_ON = 0x1,
	HALMAC_MAC_POWER_UNDEFINE = 0x7F,
};

enum halmac_wlcpu_mode {
	HALMAC_WLCPU_ACTIVE = 0x0,
	HALMAC_WLCPU_ENTER_SLEEP = 0x1,
	HALMAC_WLCPU_SLEEP = 0x2,
	HALMAC_WLCPU_UNDEFINE = 0x7F,
};

struct halmac_efuse_state {
	enum halmac_cmd_construct_state cmd_cnstr_state;
	enum halmac_cmd_process_status proc_status;
	u8 fw_rc;
	u16 seq_num;
};

struct halmac_cfg_param_state {
	enum halmac_cmd_construct_state cmd_cnstr_state;
	enum halmac_cmd_process_status proc_status;
	u8 fw_rc;
	u16 seq_num;
};

struct halmac_scan_state {
	enum halmac_cmd_construct_state cmd_cnstr_state;
	enum halmac_cmd_process_status proc_status;
	u8 fw_rc;
	u16 seq_num;
};

struct halmac_update_pkt_state {
	enum halmac_cmd_process_status proc_status;
	u8 fw_rc;
	u16 seq_num;
	u8 used_page;
};

struct halmac_scan_pkt_state {
	enum halmac_cmd_process_status proc_status;
	u8 fw_rc;
	u16 seq_num;
};

struct halmac_drop_pkt_state {
	enum halmac_cmd_process_status proc_status;
	u8 fw_rc;
	u16 seq_num;
};

struct halmac_iqk_state {
	enum halmac_cmd_process_status proc_status;
	u8 fw_rc;
	u16 seq_num;
};

struct halmac_dpk_state {
	enum halmac_cmd_process_status proc_status;
	u16 data_size;
	u16 seg_size;
	u8 *data;
	u8 fw_rc;
	u16 seq_num;
};

struct halmac_pwr_tracking_state {
	enum halmac_cmd_process_status	proc_status;
	u8 fw_rc;
	u16 seq_num;
};

struct halmac_psd_state {
	enum halmac_cmd_process_status proc_status;
	u16 data_size;
	u16 seg_size;
	u8 *data;
	u8 fw_rc;
	u16 seq_num;
};

struct halmac_fw_snding_state {
	enum halmac_cmd_construct_state cmd_cnstr_state;
	enum halmac_cmd_process_status proc_status;
	u8 fw_rc;
	u16 seq_num;
};

struct halmac_state {
	struct halmac_efuse_state efuse_state;
	struct halmac_cfg_param_state cfg_param_state;
	struct halmac_scan_state scan_state;
	struct halmac_update_pkt_state update_pkt_state;
	struct halmac_scan_pkt_state scan_pkt_state;
	struct halmac_drop_pkt_state drop_pkt_state;
	struct halmac_iqk_state iqk_state;
	struct halmac_dpk_state dpk_state;
	struct halmac_pwr_tracking_state pwr_trk_state;
	struct halmac_psd_state psd_state;
	struct halmac_fw_snding_state fw_snding_state;
	enum halmac_api_state api_state;
	enum halmac_mac_power mac_pwr;
	enum halmac_dlfw_state dlfw_state;
	enum halmac_wlcpu_mode wlcpu_mode;
	enum halmac_gpio_cfg_state gpio_cfg_state;
	enum halmac_rsvd_pg_state rsvd_pg_state;
};

#endif
