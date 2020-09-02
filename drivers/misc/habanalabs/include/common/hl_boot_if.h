/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2018-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef HL_BOOT_IF_H
#define HL_BOOT_IF_H

#define LKD_HARD_RESET_MAGIC		0xED7BD694
#define HL_POWER9_HOST_MAGIC		0x1DA30009

#define BOOT_FIT_SRAM_OFFSET		0x200000

/*
 * CPU error bits in BOOT_ERROR registers
 *
 * CPU_BOOT_ERR0_DRAM_INIT_FAIL		DRAM initialization failed.
 *					DRAM is not reliable to use.
 *
 * CPU_BOOT_ERR0_FIT_CORRUPTED		FIT data integrity verification of the
 *					image provided by the host has failed.
 *
 * CPU_BOOT_ERR0_TS_INIT_FAIL		Thermal Sensor initialization failed.
 *					Boot continues as usual, but keep in
 *					mind this is a warning.
 *
 * CPU_BOOT_ERR0_DRAM_SKIPPED		DRAM initialization has been skipped.
 *					Skipping DRAM initialization has been
 *					requested (e.g. strap, command, etc.)
 *					and FW skipped the DRAM initialization.
 *					Host can initialize the DRAM.
 *
 * CPU_BOOT_ERR0_BMC_WAIT_SKIPPED	Waiting for BMC data will be skipped.
 *					Meaning the BMC data might not be
 *					available until reset.
 *
 * CPU_BOOT_ERR0_NIC_DATA_NOT_RDY	NIC data from BMC is not ready.
 *					BMC has not provided the NIC data yet.
 *					Once provided this bit will be cleared.
 *
 * CPU_BOOT_ERR0_NIC_FW_FAIL		NIC FW loading failed.
 *					The NIC FW loading and initialization
 *					failed. This means NICs are not usable.
 *
 * CPU_BOOT_ERR0_SECURITY_NOT_RDY	Chip security initialization has been
 *					started, but is not ready yet - chip
 *					cannot be accessed.
 *
 * CPU_BOOT_ERR0_SECURITY_FAIL		Security related tasks have failed.
 *					The tasks are security init (root of
 *					trust), boot authentication (chain of
 *					trust), data packets authentication.
 *
 * CPU_BOOT_ERR0_ENABLED		Error registers enabled.
 *					This is a main indication that the
 *					running FW populates the error
 *					registers. Meaning the error bits are
 *					not garbage, but actual error statuses.
 */
#define CPU_BOOT_ERR0_DRAM_INIT_FAIL		(1 << 0)
#define CPU_BOOT_ERR0_FIT_CORRUPTED		(1 << 1)
#define CPU_BOOT_ERR0_TS_INIT_FAIL		(1 << 2)
#define CPU_BOOT_ERR0_DRAM_SKIPPED		(1 << 3)
#define CPU_BOOT_ERR0_BMC_WAIT_SKIPPED		(1 << 4)
#define CPU_BOOT_ERR0_NIC_DATA_NOT_RDY		(1 << 5)
#define CPU_BOOT_ERR0_NIC_FW_FAIL		(1 << 6)
#define CPU_BOOT_ERR0_SECURITY_NOT_RDY		(1 << 7)
#define CPU_BOOT_ERR0_SECURITY_FAIL		(1 << 8)
#define CPU_BOOT_ERR0_ENABLED			(1 << 31)

enum cpu_boot_status {
	CPU_BOOT_STATUS_NA = 0,		/* Default value after reset of chip */
	CPU_BOOT_STATUS_IN_WFE = 1,
	CPU_BOOT_STATUS_DRAM_RDY = 2,
	CPU_BOOT_STATUS_SRAM_AVAIL = 3,
	CPU_BOOT_STATUS_IN_BTL = 4,	/* BTL is H/W FSM */
	CPU_BOOT_STATUS_IN_PREBOOT = 5,
	CPU_BOOT_STATUS_IN_SPL,		/* deprecated - not reported */
	CPU_BOOT_STATUS_IN_UBOOT = 7,
	CPU_BOOT_STATUS_DRAM_INIT_FAIL,	/* deprecated - will be removed */
	CPU_BOOT_STATUS_FIT_CORRUPTED,	/* deprecated - will be removed */
	/* U-Boot console prompt activated, commands are not processed */
	CPU_BOOT_STATUS_UBOOT_NOT_READY = 10,
	/* Finished NICs init, reported after DRAM and NICs */
	CPU_BOOT_STATUS_NIC_FW_RDY = 11,
	CPU_BOOT_STATUS_TS_INIT_FAIL,	/* deprecated - will be removed */
	CPU_BOOT_STATUS_DRAM_SKIPPED,	/* deprecated - will be removed */
	CPU_BOOT_STATUS_BMC_WAITING_SKIPPED, /* deprecated - will be removed */
	/* Last boot loader progress status, ready to receive commands */
	CPU_BOOT_STATUS_READY_TO_BOOT = 15,
	/* Internal Boot finished, ready for boot-fit */
	CPU_BOOT_STATUS_WAITING_FOR_BOOT_FIT = 16,
	/* Internal Security has been initialized, device can be accessed */
	CPU_BOOT_STATUS_SECURITY_READY = 17,
};

enum kmd_msg {
	KMD_MSG_NA = 0,
	KMD_MSG_GOTO_WFE,
	KMD_MSG_FIT_RDY,
	KMD_MSG_SKIP_BMC,
};

enum cpu_msg_status {
	CPU_MSG_CLR = 0,
	CPU_MSG_OK,
	CPU_MSG_ERR,
};

#endif /* HL_BOOT_IF_H */
