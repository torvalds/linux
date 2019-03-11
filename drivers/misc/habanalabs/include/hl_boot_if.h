/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2018 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef HL_BOOT_IF_H
#define HL_BOOT_IF_H

enum cpu_boot_status {
	CPU_BOOT_STATUS_NA = 0,		/* Default value after reset of chip */
	CPU_BOOT_STATUS_IN_WFE,
	CPU_BOOT_STATUS_DRAM_RDY,
	CPU_BOOT_STATUS_SRAM_AVAIL,
	CPU_BOOT_STATUS_IN_BTL,		/* BTL is H/W FSM */
	CPU_BOOT_STATUS_IN_PREBOOT,
	CPU_BOOT_STATUS_IN_SPL,
	CPU_BOOT_STATUS_IN_UBOOT,
	CPU_BOOT_STATUS_DRAM_INIT_FAIL,
	CPU_BOOT_STATUS_FIT_CORRUPTED
};

enum kmd_msg {
	KMD_MSG_NA = 0,
	KMD_MSG_GOTO_WFE,
	KMD_MSG_FIT_RDY
};

#endif /* HL_BOOT_IF_H */
