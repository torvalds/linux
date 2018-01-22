/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef _HALMAC_FW_INFO_H_
#define _HALMAC_FW_INFO_H_

#define H2C_FORMAT_VERSION 6

#define H2C_ACK_HDR_CONTENT_LENGTH 8
#define CFG_PARAMETER_ACK_CONTENT_LENGTH 16
#define SCAN_STATUS_RPT_CONTENT_LENGTH 4
#define C2H_DBG_HEADER_LENGTH 4
#define C2H_DBG_CONTENT_MAX_LENGTH 228

#define C2H_DBG_CONTENT_SEQ_OFFSET 1

/* Rename from FW SysHalCom_Debug_RAM.h */
#define FW_REG_H2CPKT_DONE_SEQ 0x1C8
#define fw_reg_wow_reason 0x1C7

enum halmac_data_type {
	HALMAC_DATA_TYPE_MAC_REG = 0x00,
	HALMAC_DATA_TYPE_BB_REG = 0x01,
	HALMAC_DATA_TYPE_RADIO_A = 0x02,
	HALMAC_DATA_TYPE_RADIO_B = 0x03,
	HALMAC_DATA_TYPE_RADIO_C = 0x04,
	HALMAC_DATA_TYPE_RADIO_D = 0x05,

	HALMAC_DATA_TYPE_DRV_DEFINE_0 = 0x80,
	HALMAC_DATA_TYPE_DRV_DEFINE_1 = 0x81,
	HALMAC_DATA_TYPE_DRV_DEFINE_2 = 0x82,
	HALMAC_DATA_TYPE_DRV_DEFINE_3 = 0x83,
	HALMAC_DATA_TYPE_UNDEFINE = 0x7FFFFFFF,
};

enum halmac_packet_id {
	HALMAC_PACKET_PROBE_REQ = 0x00,
	HALMAC_PACKET_SYNC_BCN = 0x01,
	HALMAC_PACKET_DISCOVERY_BCN = 0x02,

	HALMAC_PACKET_UNDEFINE = 0x7FFFFFFF,
};

/* Channel Switch Action ID */
enum halmac_cs_action_id {
	HALMAC_CS_ACTION_NONE = 0x00,
	HALMAC_CS_ACTIVE_SCAN = 0x01,
	HALMAC_CS_NAN_NONMASTER_DW = 0x02,
	HALMAC_CS_NAN_NONMASTER_NONDW = 0x03,
	HALMAC_CS_NAN_MASTER_NONDW = 0x04,
	HALMAC_CS_NAN_MASTER_DW = 0x05,

	HALMAC_CS_ACTION_UNDEFINE = 0x7FFFFFFF,
};

/* Channel Switch Extra Action ID */
enum halmac_cs_extra_action_id {
	HALMAC_CS_EXTRA_ACTION_NONE = 0x00,
	HALMAC_CS_EXTRA_UPDATE_PROBE = 0x01,
	HALMAC_CS_EXTRA_UPDATE_BEACON = 0x02,

	HALMAC_CS_EXTRA_ACTION_UNDEFINE = 0x7FFFFFFF,
};

enum halmac_h2c_return_code {
	HALMAC_H2C_RETURN_SUCCESS = 0x00,
	HALMAC_H2C_RETURN_CFG_ERR_LEN = 0x01,
	HALMAC_H2C_RETURN_CFG_ERR_CMD = 0x02,

	HALMAC_H2C_RETURN_EFUSE_ERR_DUMP = 0x03,

	HALMAC_H2C_RETURN_DATAPACK_ERR_FULL = 0x04, /* DMEM buffer full */
	HALMAC_H2C_RETURN_DATAPACK_ERR_ID = 0x05, /* Invalid pack id */

	HALMAC_H2C_RETURN_RUN_ERR_EMPTY =
		0x06, /* No data in dedicated buffer */
	HALMAC_H2C_RETURN_RUN_ERR_LEN = 0x07,
	HALMAC_H2C_RETURN_RUN_ERR_CMD = 0x08,
	HALMAC_H2C_RETURN_RUN_ERR_ID = 0x09, /* Invalid pack id */

	HALMAC_H2C_RETURN_PACKET_ERR_FULL = 0x0A, /* DMEM buffer full */
	HALMAC_H2C_RETURN_PACKET_ERR_ID = 0x0B, /* Invalid packet id */

	HALMAC_H2C_RETURN_SCAN_ERR_FULL = 0x0C, /* DMEM buffer full */
	HALMAC_H2C_RETURN_SCAN_ERR_PHYDM = 0x0D, /* PHYDM API return fail */

	HALMAC_H2C_RETURN_ORIG_ERR_ID = 0x0E, /* Invalid original H2C cmd id */

	HALMAC_H2C_RETURN_UNDEFINE = 0x7FFFFFFF,
};

enum halmac_scan_report_code {
	HALMAC_SCAN_REPORT_DONE = 0x00,
	HALMAC_SCAN_REPORT_ERR_PHYDM = 0x01, /* PHYDM API return fail */
	HALMAC_SCAN_REPORT_ERR_ID = 0x02, /* Invalid ActionID */
	HALMAC_SCAN_REPORT_ERR_TX = 0x03, /* Tx RsvdPage fail */

	HALMAC_SCAN_REPORT_UNDEFINE = 0x7FFFFFFF,
};

#endif
