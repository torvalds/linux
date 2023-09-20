/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2019-2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDI2_FW_IF_H
#define GAUDI2_FW_IF_H

#define GAUDI2_EVENT_QUEUE_MSIX_IDX	0

#define UBOOT_FW_OFFSET			0x100000	/* 1MB in SRAM */
#define LINUX_FW_OFFSET			0x800000	/* 8BM in DDR */

#define GAUDI2_PLL_FREQ_LOW		200000000 /* 200 MHz */

#define GAUDI2_SP_SRAM_BASE_ADDR	0x27FE0000
#define GAUDI2_MAILBOX_BASE_ADDR	0x27FE1800

#define GAUDI2_NUM_MME			4

#define NUM_OF_GPIOS_PER_PORT		16
#define GAUDI2_WD_GPIO			(62 % NUM_OF_GPIOS_PER_PORT)

#define GAUDI2_ARCPID_TX_MB_SIZE	0x1000
#define GAUDI2_ARCPID_RX_MB_SIZE	0x400
#define GAUDI2_ARM_TX_MB_SIZE		0x400
#define GAUDI2_ARM_RX_MB_SIZE		0x1800

#define GAUDI2_DCCM_BASE_ADDR		0x27020000

#define GAUDI2_ARM_TX_MB_ADDR		GAUDI2_MAILBOX_BASE_ADDR

#define GAUDI2_ARM_RX_MB_ADDR		(GAUDI2_ARM_TX_MB_ADDR + \
					GAUDI2_ARM_TX_MB_SIZE)

#define GAUDI2_ARCPID_TX_MB_ADDR	(GAUDI2_ARM_RX_MB_ADDR + GAUDI2_ARM_RX_MB_SIZE)

#define GAUDI2_ARCPID_RX_MB_ADDR	(GAUDI2_ARCPID_TX_MB_ADDR + GAUDI2_ARCPID_TX_MB_SIZE)

#define GAUDI2_ARM_TX_MB_OFFSET		(GAUDI2_ARM_TX_MB_ADDR - \
					GAUDI2_SP_SRAM_BASE_ADDR)

#define GAUDI2_ARM_RX_MB_OFFSET		(GAUDI2_ARM_RX_MB_ADDR - \
					GAUDI2_SP_SRAM_BASE_ADDR)

enum gaudi2_fw_status {
	GAUDI2_PID_STATUS_UP = 0x1,	/* PID on ARC0 is up */
	GAUDI2_ARM_STATUS_UP = 0x2,	/* ARM Linux Boot complete */
	GAUDI2_MGMT_STATUS_UP = 0x3,	/* ARC1 Mgmt is up */
	GAUDI2_STATUS_LAST = 0xFF
};

struct gaudi2_cold_rst_data {
	union {
		struct {
			u32 recovery_flag: 1;
			u32 validation_flag: 1;
			u32 efuse_read_flag: 1;
			u32 spsram_init_done : 1;
			u32 fake_security_enable : 1;
			u32 fake_sig_validation_en : 1;
			u32 bist_skip_enable : 1;
			u32 reserved1 : 1;
			u32 fake_bis_compliant : 1;
			u32 wd_rst_cause_arm : 1;
			u32 wd_rst_cause_arcpid : 1;
			u32 reserved : 21;
		};
		__le32 data;
	};
};

enum gaudi2_rst_src {
	HL_COLD_RST = 1,
	HL_MANUAL_RST = 2,
	HL_PRSTN_RST = 4,
	HL_SOFT_RST = 8,
	HL_WD_RST = 16,
	HL_FW_ALL_RST = 32,
	HL_SW_ALL_RST = 64,
	HL_FLR_RST = 128,
	HL_ECC_DERR_RST = 256
};

struct gaudi2_redundancy_ctx {
	__le32 redundant_hbm;
	__le32 redundant_edma;
	__le32 redundant_tpc;
	__le32 redundant_vdec;
	__le64 hbm_mask;
	__le64 edma_mask;
	__le64 tpc_mask;
	__le64 vdec_mask;
	__le64 mme_mask;
	__le64 nic_mask;
	__le64 rtr_mask;
	__le64 hmmu_hif_iso;
	__le64 xbar_edge_iso;
	__le64 hmmu_hif_mask;
	__le64 xbar_edge_mask;
	__u8 mme_pe_iso[GAUDI2_NUM_MME];
	__le32 full_hbm_mode;	/* true on full (non binning hbm)*/
} __packed;

#endif /* GAUDI2_FW_IF_H */
