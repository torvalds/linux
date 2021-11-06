/*
 * Header file for DHD daemon to handle timeouts
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef __BCM_DHDD_H__
#define __BCM_DHDD_H__

#include <brcm_nl80211.h>
/**
 * To maintain compatabily when dhd driver and dhd daemon is taken from different branches,
 * make sure to keep this file same across dhd driver branch and dhd apps branch.
 * TODO: Make this file as shared between apps and dhd.ko
 */

#define BCM_TO_MAGIC 0x600DB055
#define NO_TRAP 0
#define DO_TRAP	1

typedef enum notify_dhd_daemon_reason {
	REASON_COMMAND_TO,
	REASON_OQS_TO,
	REASON_SCAN_TO,
	REASON_JOIN_TO,
	REASON_DAEMON_STARTED,
	REASON_DEVICE_TX_STUCK_WARNING,
	REASON_DEVICE_TX_STUCK,
	REASON_UNKOWN
} notify_dhd_daemon_reason_t;

typedef struct bcm_to_info {
	int magic;
	int reason;
	int trap;
} bcm_to_info_t;

#endif /* __BCM_DHDD_H__ */
