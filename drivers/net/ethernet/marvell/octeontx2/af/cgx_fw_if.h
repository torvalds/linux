/* SPDX-License-Identifier: GPL-2.0 */
/*  Marvell OcteonTx2 CGX driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __CGX_FW_INTF_H__
#define __CGX_FW_INTF_H__

#include <linux/bitops.h>
#include <linux/bitfield.h>

#define CGX_FIRMWARE_MAJOR_VER		1
#define CGX_FIRMWARE_MINOR_VER		0

#define CGX_EVENT_ACK                   1UL

/* CGX error types. set for cmd response status as CGX_STAT_FAIL */
enum cgx_error_type {
	CGX_ERR_NONE,
	CGX_ERR_LMAC_NOT_ENABLED,
	CGX_ERR_LMAC_MODE_INVALID,
	CGX_ERR_REQUEST_ID_INVALID,
	CGX_ERR_PREV_ACK_NOT_CLEAR,
	CGX_ERR_PHY_LINK_DOWN,
	CGX_ERR_PCS_RESET_FAIL,
	CGX_ERR_AN_CPT_FAIL,
	CGX_ERR_TX_NOT_IDLE,
	CGX_ERR_RX_NOT_IDLE,
	CGX_ERR_SPUX_BR_BLKLOCK_FAIL,
	CGX_ERR_SPUX_RX_ALIGN_FAIL,
	CGX_ERR_SPUX_TX_FAULT,
	CGX_ERR_SPUX_RX_FAULT,
	CGX_ERR_SPUX_RESET_FAIL,
	CGX_ERR_SPUX_AN_RESET_FAIL,
	CGX_ERR_SPUX_USX_AN_RESET_FAIL,
	CGX_ERR_SMUX_RX_LINK_NOT_OK,
	CGX_ERR_PCS_RECV_LINK_FAIL,
	CGX_ERR_TRAINING_FAIL,
	CGX_ERR_RX_EQU_FAIL,
	CGX_ERR_SPUX_BER_FAIL,
	CGX_ERR_SPUX_RSFEC_ALGN_FAIL,   /* = 22 */
};

/* LINK speed types */
enum cgx_link_speed {
	CGX_LINK_NONE,
	CGX_LINK_10M,
	CGX_LINK_100M,
	CGX_LINK_1G,
	CGX_LINK_2HG,
	CGX_LINK_5G,
	CGX_LINK_10G,
	CGX_LINK_20G,
	CGX_LINK_25G,
	CGX_LINK_40G,
	CGX_LINK_50G,
	CGX_LINK_100G,
	CGX_LINK_SPEED_MAX,
};

/* REQUEST ID types. Input to firmware */
enum cgx_cmd_id {
	CGX_CMD_NONE,
	CGX_CMD_GET_FW_VER,
	CGX_CMD_GET_MAC_ADDR,
	CGX_CMD_SET_MTU,
	CGX_CMD_GET_LINK_STS,		/* optional to user */
	CGX_CMD_LINK_BRING_UP,
	CGX_CMD_LINK_BRING_DOWN,
	CGX_CMD_INTERNAL_LBK,
	CGX_CMD_EXTERNAL_LBK,
	CGX_CMD_HIGIG,
	CGX_CMD_LINK_STATE_CHANGE,
	CGX_CMD_MODE_CHANGE,		/* hot plug support */
	CGX_CMD_INTF_SHUTDOWN,
	CGX_CMD_GET_MKEX_PRFL_SIZE,
	CGX_CMD_GET_MKEX_PRFL_ADDR,
	CGX_CMD_GET_FWD_BASE,		/* get base address of shared FW data */
};

/* async event ids */
enum cgx_evt_id {
	CGX_EVT_NONE,
	CGX_EVT_LINK_CHANGE,
};

/* event types - cause of interrupt */
enum cgx_evt_type {
	CGX_EVT_ASYNC,
	CGX_EVT_CMD_RESP
};

enum cgx_stat {
	CGX_STAT_SUCCESS,
	CGX_STAT_FAIL
};

enum cgx_cmd_own {
	CGX_CMD_OWN_NS,
	CGX_CMD_OWN_FIRMWARE,
};

/* m - bit mask
 * y - value to be written in the bitrange
 * x - input value whose bitrange to be modified
 */
#define FIELD_SET(m, y, x)		\
	(((x) & ~(m)) |			\
	FIELD_PREP((m), (y)))

/* scratchx(0) CSR used for ATF->non-secure SW communication.
 * This acts as the status register
 * Provides details on command ack/status, command response, error details
 */
#define EVTREG_ACK		BIT_ULL(0)
#define EVTREG_EVT_TYPE		BIT_ULL(1)
#define EVTREG_STAT		BIT_ULL(2)
#define EVTREG_ID		GENMASK_ULL(8, 3)

/* Response to command IDs with command status as CGX_STAT_FAIL
 *
 * Not applicable for commands :
 * CGX_CMD_LINK_BRING_UP/DOWN/CGX_EVT_LINK_CHANGE
 */
#define EVTREG_ERRTYPE		GENMASK_ULL(18, 9)

/* Response to cmd ID as CGX_CMD_GET_FW_VER with cmd status as
 * CGX_STAT_SUCCESS
 */
#define RESP_MAJOR_VER		GENMASK_ULL(12, 9)
#define RESP_MINOR_VER		GENMASK_ULL(16, 13)

/* Response to cmd ID as CGX_CMD_GET_MAC_ADDR with cmd status as
 * CGX_STAT_SUCCESS
 */
#define RESP_MAC_ADDR		GENMASK_ULL(56, 9)

/* Response to cmd ID as CGX_CMD_GET_MKEX_PRFL_SIZE with cmd status as
 * CGX_STAT_SUCCESS
 */
#define RESP_MKEX_PRFL_SIZE		GENMASK_ULL(63, 9)

/* Response to cmd ID as CGX_CMD_GET_MKEX_PRFL_ADDR with cmd status as
 * CGX_STAT_SUCCESS
 */
#define RESP_MKEX_PRFL_ADDR		GENMASK_ULL(63, 9)

/* Response to cmd ID as CGX_CMD_GET_FWD_BASE with cmd status as
 * CGX_STAT_SUCCESS
 */
#define RESP_FWD_BASE		GENMASK_ULL(56, 9)

/* Response to cmd ID - CGX_CMD_LINK_BRING_UP/DOWN, event ID CGX_EVT_LINK_CHANGE
 * status can be either CGX_STAT_FAIL or CGX_STAT_SUCCESS
 *
 * In case of CGX_STAT_FAIL, it indicates CGX configuration failed
 * when processing link up/down/change command.
 * Both err_type and current link status will be updated
 *
 * In case of CGX_STAT_SUCCESS, err_type will be CGX_ERR_NONE and current
 * link status will be updated
 */
struct cgx_lnk_sts {
	uint64_t reserved1:9;
	uint64_t link_up:1;
	uint64_t full_duplex:1;
	uint64_t speed:4;		/* cgx_link_speed */
	uint64_t err_type:10;
	uint64_t reserved2:39;
};

#define RESP_LINKSTAT_UP		GENMASK_ULL(9, 9)
#define RESP_LINKSTAT_FDUPLEX		GENMASK_ULL(10, 10)
#define RESP_LINKSTAT_SPEED		GENMASK_ULL(14, 11)
#define RESP_LINKSTAT_ERRTYPE		GENMASK_ULL(24, 15)

/* scratchx(1) CSR used for non-secure SW->ATF communication
 * This CSR acts as a command register
 */
#define CMDREG_OWN	BIT_ULL(0)
#define CMDREG_ID	GENMASK_ULL(7, 2)

/* Any command using enable/disable as an argument need
 * to set this bitfield.
 * Ex: Loopback, HiGig...
 */
#define CMDREG_ENABLE	BIT_ULL(8)

/* command argument to be passed for cmd ID - CGX_CMD_SET_MTU */
#define CMDMTU_SIZE	GENMASK_ULL(23, 8)

/* command argument to be passed for cmd ID - CGX_CMD_LINK_CHANGE */
#define CMDLINKCHANGE_LINKUP	BIT_ULL(8)
#define CMDLINKCHANGE_FULLDPLX	BIT_ULL(9)
#define CMDLINKCHANGE_SPEED	GENMASK_ULL(13, 10)

#endif /* __CGX_FW_INTF_H__ */
