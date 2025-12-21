/* SPDX-License-Identifier: ISC */
/*
 * Copyright (c) 2014,2017 Qualcomm Atheros, Inc.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

/* "API" level of the ath10k testmode interface. Bump it after every
 * incompatible interface change.
 */
#define ATH10K_TESTMODE_VERSION_MAJOR 1

/* Bump this after every _compatible_ interface change, for example
 * addition of a new command or an attribute.
 */
#define ATH10K_TESTMODE_VERSION_MINOR 0

#define ATH10K_TM_DATA_MAX_LEN		5000
#define ATH_FTM_EVENT_MAX_BUF_LENGTH	2048

enum ath10k_tm_attr {
	__ATH10K_TM_ATTR_INVALID	= 0,
	ATH10K_TM_ATTR_CMD		= 1,
	ATH10K_TM_ATTR_DATA		= 2,
	ATH10K_TM_ATTR_WMI_CMDID	= 3,
	ATH10K_TM_ATTR_VERSION_MAJOR	= 4,
	ATH10K_TM_ATTR_VERSION_MINOR	= 5,
	ATH10K_TM_ATTR_WMI_OP_VERSION	= 6,

	/* keep last */
	__ATH10K_TM_ATTR_AFTER_LAST,
	ATH10K_TM_ATTR_MAX		= __ATH10K_TM_ATTR_AFTER_LAST - 1,
};

/* All ath10k testmode interface commands specified in
 * ATH10K_TM_ATTR_CMD
 */
enum ath10k_tm_cmd {
	/* Returns the supported ath10k testmode interface version in
	 * ATH10K_TM_ATTR_VERSION. Always guaranteed to work. User space
	 * uses this to verify it's using the correct version of the
	 * testmode interface
	 */
	ATH10K_TM_CMD_GET_VERSION = 0,

	/* Boots the UTF firmware, the netdev interface must be down at the
	 * time.
	 */
	ATH10K_TM_CMD_UTF_START = 1,

	/* Shuts down the UTF firmware and puts the driver back into OFF
	 * state.
	 */
	ATH10K_TM_CMD_UTF_STOP = 2,

	/* The command used to transmit a WMI command to the firmware and
	 * the event to receive WMI events from the firmware. Without
	 * struct wmi_cmd_hdr header, only the WMI payload. Command id is
	 * provided with ATH10K_TM_ATTR_WMI_CMDID and payload in
	 * ATH10K_TM_ATTR_DATA.
	 */
	ATH10K_TM_CMD_WMI = 3,

	/* The command used to transmit a test command to the firmware
	 * and the event to receive test events from the firmware. The data
	 * received only contain the TLV payload, need to add the tlv header
	 * and send the cmd to firmware with command id WMI_PDEV_UTF_CMDID.
	 * The data payload size could be large and the driver needs to
	 * send segmented data to firmware.
	 *
	 * This legacy testmode command shares the same value as the get-version
	 * command. To distinguish between them, we check whether the data attribute
	 * is present.
	 */
	ATH10K_TM_CMD_TLV = ATH10K_TM_CMD_GET_VERSION,
};
