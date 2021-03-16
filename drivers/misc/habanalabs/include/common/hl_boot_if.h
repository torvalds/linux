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
 * CPU_BOOT_ERR0_EFUSE_FAIL		Reading from eFuse failed.
 *					The PCI device ID might be wrong.
 *
 * CPU_BOOT_ERR0_PRI_IMG_VER_FAIL	Verification of primary image failed.
 *					It mean that ppboot checksum
 *					verification for the preboot primary
 *					image has failed to match expected
 *					checksum. Trying to program image again
 *					might solve this.
 *
 * CPU_BOOT_ERR0_SEC_IMG_VER_FAIL	Verification of secondary image failed.
 *					It mean that ppboot checksum
 *					verification for the preboot secondary
 *					image has failed to match expected
 *					checksum. Trying to program image again
 *					might solve this.
 *
 * CPU_BOOT_ERR0_PLL_FAIL		PLL settings failed, meaning that one
 *					of the PLLs remains in REF_CLK
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
#define CPU_BOOT_ERR0_EFUSE_FAIL		(1 << 9)
#define CPU_BOOT_ERR0_PRI_IMG_VER_FAIL		(1 << 10)
#define CPU_BOOT_ERR0_SEC_IMG_VER_FAIL		(1 << 11)
#define CPU_BOOT_ERR0_PLL_FAIL			(1 << 12)
#define CPU_BOOT_ERR0_ENABLED			(1 << 31)

/*
 * BOOT DEVICE STATUS bits in BOOT_DEVICE_STS registers
 *
 * CPU_BOOT_DEV_STS0_SECURITY_EN	Security is Enabled.
 *					This is an indication for security
 *					enabled in FW, which means that
 *					all conditions for security are met:
 *					device is indicated as security enabled,
 *					registers are protected, and device
 *					uses keys for image verification.
 *					Initialized in: preboot
 *
 * CPU_BOOT_DEV_STS0_DEBUG_EN		Debug is enabled.
 *					Enabled when JTAG or DEBUG is enabled
 *					in FW.
 *					Initialized in: preboot
 *
 * CPU_BOOT_DEV_STS0_WATCHDOG_EN	Watchdog is enabled.
 *					Watchdog is enabled in FW.
 *					Initialized in: preboot
 *
 * CPU_BOOT_DEV_STS0_DRAM_INIT_EN	DRAM initialization is enabled.
 *					DRAM initialization has been done in FW.
 *					Initialized in: u-boot
 *
 * CPU_BOOT_DEV_STS0_BMC_WAIT_EN	Waiting for BMC data enabled.
 *					If set, it means that during boot,
 *					FW waited for BMC data.
 *					Initialized in: u-boot
 *
 * CPU_BOOT_DEV_STS0_E2E_CRED_EN	E2E credits initialized.
 *					FW initialized E2E credits.
 *					Initialized in: u-boot
 *
 * CPU_BOOT_DEV_STS0_HBM_CRED_EN	HBM credits initialized.
 *					FW initialized HBM credits.
 *					Initialized in: u-boot
 *
 * CPU_BOOT_DEV_STS0_RL_EN		Rate limiter initialized.
 *					FW initialized rate limiter.
 *					Initialized in: u-boot
 *
 * CPU_BOOT_DEV_STS0_SRAM_SCR_EN	SRAM scrambler enabled.
 *					FW initialized SRAM scrambler.
 *					Initialized in: linux
 *
 * CPU_BOOT_DEV_STS0_DRAM_SCR_EN	DRAM scrambler enabled.
 *					FW initialized DRAM scrambler.
 *					Initialized in: u-boot
 *
 * CPU_BOOT_DEV_STS0_FW_HARD_RST_EN	FW hard reset procedure is enabled.
 *					FW has the hard reset procedure
 *					implemented. This means that FW will
 *					perform hard reset procedure on
 *					receiving the halt-machine event.
 *					Initialized in: preboot, u-boot, linux
 *
 * CPU_BOOT_DEV_STS0_PLL_INFO_EN	FW retrieval of PLL info is enabled.
 *					Initialized in: linux
 *
 * CPU_BOOT_DEV_STS0_SP_SRAM_EN		SP SRAM is initialized and available
 *					for use.
 *					Initialized in: preboot
 *
 * CPU_BOOT_DEV_STS0_CLK_GATE_EN	Clock Gating enabled.
 *					FW initialized Clock Gating.
 *					Initialized in: preboot
 *
 * CPU_BOOT_DEV_STS0_HBM_ECC_EN		HBM ECC handling Enabled.
 *					FW handles HBM ECC indications.
 *					Initialized in: linux
 *
 * CPU_BOOT_DEV_STS0_PKT_PI_ACK_EN	Packets ack value used in the armcpd
 *					is set to the PI counter.
 *					Initialized in: linux
 *
 * CPU_BOOT_DEV_STS0_ENABLED		Device status register enabled.
 *					This is a main indication that the
 *					running FW populates the device status
 *					register. Meaning the device status
 *					bits are not garbage, but actual
 *					statuses.
 *					Initialized in: preboot
 *
 */
#define CPU_BOOT_DEV_STS0_SECURITY_EN			(1 << 0)
#define CPU_BOOT_DEV_STS0_DEBUG_EN			(1 << 1)
#define CPU_BOOT_DEV_STS0_WATCHDOG_EN			(1 << 2)
#define CPU_BOOT_DEV_STS0_DRAM_INIT_EN			(1 << 3)
#define CPU_BOOT_DEV_STS0_BMC_WAIT_EN			(1 << 4)
#define CPU_BOOT_DEV_STS0_E2E_CRED_EN			(1 << 5)
#define CPU_BOOT_DEV_STS0_HBM_CRED_EN			(1 << 6)
#define CPU_BOOT_DEV_STS0_RL_EN				(1 << 7)
#define CPU_BOOT_DEV_STS0_SRAM_SCR_EN			(1 << 8)
#define CPU_BOOT_DEV_STS0_DRAM_SCR_EN			(1 << 9)
#define CPU_BOOT_DEV_STS0_FW_HARD_RST_EN		(1 << 10)
#define CPU_BOOT_DEV_STS0_PLL_INFO_EN			(1 << 11)
#define CPU_BOOT_DEV_STS0_SP_SRAM_EN			(1 << 12)
#define CPU_BOOT_DEV_STS0_CLK_GATE_EN			(1 << 13)
#define CPU_BOOT_DEV_STS0_HBM_ECC_EN			(1 << 14)
#define CPU_BOOT_DEV_STS0_PKT_PI_ACK_EN			(1 << 15)
#define CPU_BOOT_DEV_STS0_ENABLED			(1 << 31)

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
	RESERVED,
	KMD_MSG_RST_DEV,
};

enum cpu_msg_status {
	CPU_MSG_CLR = 0,
	CPU_MSG_OK,
	CPU_MSG_ERR,
};

#endif /* HL_BOOT_IF_H */
