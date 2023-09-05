/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

/* "API" level of the ath11k testmode interface. Bump it after every
 * incompatible interface change.
 */
#define ATH11K_TESTMODE_VERSION_MAJOR 1

/* Bump this after every _compatible_ interface change, for example
 * addition of a new command or an attribute.
 */
#define ATH11K_TESTMODE_VERSION_MINOR 1

#define ATH11K_TM_DATA_MAX_LEN		5000
#define ATH11K_FTM_EVENT_MAX_BUF_LENGTH 2048

enum ath11k_tm_attr {
	__ATH11K_TM_ATTR_INVALID		= 0,
	ATH11K_TM_ATTR_CMD			= 1,
	ATH11K_TM_ATTR_DATA			= 2,
	ATH11K_TM_ATTR_WMI_CMDID		= 3,
	ATH11K_TM_ATTR_VERSION_MAJOR		= 4,
	ATH11K_TM_ATTR_VERSION_MINOR		= 5,
	ATH11K_TM_ATTR_WMI_OP_VERSION		= 6,

	/* keep last */
	__ATH11K_TM_ATTR_AFTER_LAST,
	ATH11K_TM_ATTR_MAX		= __ATH11K_TM_ATTR_AFTER_LAST - 1,
};

/* All ath11k testmode interface commands specified in
 * ATH11K_TM_ATTR_CMD
 */
enum ath11k_tm_cmd {
	/* Returns the supported ath11k testmode interface version in
	 * ATH11K_TM_ATTR_VERSION. Always guaranteed to work. User space
	 * uses this to verify it's using the correct version of the
	 * testmode interface
	 */
	ATH11K_TM_CMD_GET_VERSION = 0,

	/* The command used to transmit a WMI command to the firmware and
	 * the event to receive WMI events from the firmware. Without
	 * struct wmi_cmd_hdr header, only the WMI payload. Command id is
	 * provided with ATH11K_TM_ATTR_WMI_CMDID and payload in
	 * ATH11K_TM_ATTR_DATA.
	 */
	ATH11K_TM_CMD_WMI = 1,

	/* Boots the UTF firmware, the netdev interface must be down at the
	 * time.
	 */
	ATH11K_TM_CMD_TESTMODE_START = 2,

	/* The command used to transmit a FTM WMI command to the firmware
	 * and the event to receive WMI events from the firmware. The data
	 * received only contain the payload, need to add the tlv header
	 * and send the cmd to firmware with command id WMI_PDEV_UTF_CMDID.
	 * The data payload size could be large and the driver needs to
	 * send segmented data to firmware.
	 */
	ATH11K_TM_CMD_WMI_FTM = 3,
};
