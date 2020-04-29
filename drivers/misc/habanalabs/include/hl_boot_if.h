/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2018 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef HL_BOOT_IF_H
#define HL_BOOT_IF_H

#define LKD_HARD_RESET_MAGIC		0xED7BD694

/* CPU error bits in BOOT_ERROR registers */
#define CPU_BOOT_ERR0_DRAM_INIT_FAIL		(1 << 0)
#define CPU_BOOT_ERR0_FIT_CORRUPTED		(1 << 1)
#define CPU_BOOT_ERR0_TS_INIT_FAIL		(1 << 2)
#define CPU_BOOT_ERR0_DRAM_SKIPPED		(1 << 3)
#define CPU_BOOT_ERR0_BMC_WAIT_SKIPPED		(1 << 4)
#define CPU_BOOT_ERR0_NIC_DATA_NOT_RDY		(1 << 5)
#define CPU_BOOT_ERR0_NIC_FW_FAIL		(1 << 6)
#define CPU_BOOT_ERR0_ENABLED			(1 << 31)

enum cpu_boot_status {
	CPU_BOOT_STATUS_NA = 0,		/* Default value after reset of chip */
	CPU_BOOT_STATUS_IN_WFE = 1,
	CPU_BOOT_STATUS_DRAM_RDY = 2,
	CPU_BOOT_STATUS_SRAM_AVAIL = 3,
	CPU_BOOT_STATUS_IN_BTL = 4,	/* BTL is H/W FSM */
	CPU_BOOT_STATUS_IN_PREBOOT = 5,
	CPU_BOOT_STATUS_IN_SPL = 6,
	CPU_BOOT_STATUS_IN_UBOOT = 7,
	CPU_BOOT_STATUS_DRAM_INIT_FAIL,	/* deprecated - will be removed */
	CPU_BOOT_STATUS_FIT_CORRUPTED,	/* deprecated - will be removed */
	CPU_BOOT_STATUS_UBOOT_NOT_READY = 10,
	CPU_BOOT_STATUS_NIC_FW_RDY = 11,
	CPU_BOOT_STATUS_TS_INIT_FAIL,	/* deprecated - will be removed */
	CPU_BOOT_STATUS_DRAM_SKIPPED,	/* deprecated - will be removed */
	CPU_BOOT_STATUS_BMC_WAITING_SKIPPED, /* deprecated - will be removed */
	CPU_BOOT_STATUS_READY_TO_BOOT = 15,
};

enum kmd_msg {
	KMD_MSG_NA = 0,
	KMD_MSG_GOTO_WFE,
	KMD_MSG_FIT_RDY
};

#endif /* HL_BOOT_IF_H */
