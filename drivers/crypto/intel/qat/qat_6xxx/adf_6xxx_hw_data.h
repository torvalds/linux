/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2025 Intel Corporation */
#ifndef ADF_6XXX_HW_DATA_H_
#define ADF_6XXX_HW_DATA_H_

#include <linux/bits.h>
#include <linux/time.h>
#include <linux/units.h>

#include "adf_accel_devices.h"
#include "adf_cfg_common.h"
#include "adf_dc.h"

/* PCIe configuration space */
#define ADF_GEN6_BAR_MASK		(BIT(0) | BIT(2) | BIT(4))
#define ADF_GEN6_SRAM_BAR		0
#define ADF_GEN6_PMISC_BAR		1
#define ADF_GEN6_ETR_BAR		2
#define ADF_6XXX_MAX_ACCELENGINES	9

/* Clocks frequency */
#define ADF_GEN6_COUNTER_FREQ		(100 * HZ_PER_MHZ)

/* Physical function fuses */
#define ADF_GEN6_FUSECTL0_OFFSET	0x2C8
#define ADF_GEN6_FUSECTL1_OFFSET	0x2CC
#define ADF_GEN6_FUSECTL4_OFFSET	0x2D8

/* Accelerators */
#define ADF_GEN6_ACCELERATORS_MASK	0x1
#define ADF_GEN6_MAX_ACCELERATORS	1

/* MSI-X interrupt */
#define ADF_GEN6_SMIAPF_RP_X0_MASK_OFFSET	0x41A040
#define ADF_GEN6_SMIAPF_RP_X1_MASK_OFFSET	0x41A044
#define ADF_GEN6_SMIAPF_MASK_OFFSET		0x41A084
#define ADF_GEN6_MSIX_RTTABLE_OFFSET(i)		(0x409000 + ((i) * 4))

/* Bank and ring configuration */
#define ADF_GEN6_NUM_RINGS_PER_BANK	2
#define ADF_GEN6_NUM_BANKS_PER_VF	4
#define ADF_GEN6_ETR_MAX_BANKS		64
#define ADF_GEN6_RX_RINGS_OFFSET	1
#define ADF_GEN6_TX_RINGS_MASK		0x1

/* Arbiter configuration */
#define ADF_GEN6_ARB_CONFIG			(BIT(31) | BIT(6) | BIT(0))
#define ADF_GEN6_ARB_OFFSET			0x000
#define ADF_GEN6_ARB_WRK_2_SER_MAP_OFFSET	0x400

/* Admin interface configuration */
#define ADF_GEN6_ADMINMSGUR_OFFSET	0x500574
#define ADF_GEN6_ADMINMSGLR_OFFSET	0x500578
#define ADF_GEN6_MAILBOX_BASE_OFFSET	0x600970

/*
 * Watchdog timers
 * Timeout is in cycles. Clock speed may vary across products but this
 * value should be a few milli-seconds.
 */
#define ADF_SSM_WDT_DEFAULT_VALUE	0x7000000ULL
#define ADF_SSM_WDT_PKE_DEFAULT_VALUE	0x8000000ULL
#define ADF_SSMWDTATHL_OFFSET		0x5208
#define ADF_SSMWDTATHH_OFFSET		0x520C
#define ADF_SSMWDTCNVL_OFFSET		0x5408
#define ADF_SSMWDTCNVH_OFFSET		0x540C
#define ADF_SSMWDTUCSL_OFFSET		0x5808
#define ADF_SSMWDTUCSH_OFFSET		0x580C
#define ADF_SSMWDTDCPRL_OFFSET		0x5A08
#define ADF_SSMWDTDCPRH_OFFSET		0x5A0C
#define ADF_SSMWDTPKEL_OFFSET		0x5E08
#define ADF_SSMWDTPKEH_OFFSET		0x5E0C

/* Ring reset */
#define ADF_RPRESET_POLL_TIMEOUT_US	(5 * USEC_PER_SEC)
#define ADF_RPRESET_POLL_DELAY_US	20
#define ADF_WQM_CSR_RPRESETCTL_RESET	BIT(0)
#define ADF_WQM_CSR_RPRESETCTL(bank)	(0x6000 + (bank) * 8)
#define ADF_WQM_CSR_RPRESETSTS_STATUS	BIT(0)
#define ADF_WQM_CSR_RPRESETSTS(bank)	(ADF_WQM_CSR_RPRESETCTL(bank) + 4)

/* Controls and sets up the corresponding ring mode of operation */
#define ADF_GEN6_CSR_RINGMODECTL(bank)		(0x9000 + (bank) * 4)

/* Specifies the traffic class to use for the transactions to/from the ring */
#define ADF_GEN6_RINGMODECTL_TC_MASK		GENMASK(18, 16)
#define ADF_GEN6_RINGMODECTL_TC_DEFAULT		0x7

/* Specifies usage of tc for the transactions to/from this ring */
#define ADF_GEN6_RINGMODECTL_TC_EN_MASK		GENMASK(20, 19)

/*
 * Use the value programmed in the tc field for request descriptor
 * and metadata read transactions
 */
#define ADF_GEN6_RINGMODECTL_TC_EN_OP1		0x1

/* VC0 Resource Control Register */
#define ADF_GEN6_PVC0CTL_OFFSET			0x204
#define ADF_GEN6_PVC0CTL_TCVCMAP_OFFSET		1
#define ADF_GEN6_PVC0CTL_TCVCMAP_MASK		GENMASK(7, 1)
#define ADF_GEN6_PVC0CTL_TCVCMAP_DEFAULT	0x3F

/* VC1 Resource Control Register */
#define ADF_GEN6_PVC1CTL_OFFSET			0x210
#define ADF_GEN6_PVC1CTL_TCVCMAP_OFFSET		1
#define ADF_GEN6_PVC1CTL_TCVCMAP_MASK		GENMASK(7, 1)
#define ADF_GEN6_PVC1CTL_TCVCMAP_DEFAULT	0x40
#define ADF_GEN6_PVC1CTL_VCEN_OFFSET		31
#define ADF_GEN6_PVC1CTL_VCEN_MASK		BIT(31)
/* RW bit: 0x1 - enables a Virtual Channel, 0x0 - disables */
#define ADF_GEN6_PVC1CTL_VCEN_ON		0x1

/* Error source mask registers */
#define ADF_GEN6_ERRMSK0	0x41A210
#define ADF_GEN6_ERRMSK1	0x41A214
#define ADF_GEN6_ERRMSK2	0x41A218
#define ADF_GEN6_ERRMSK3	0x41A21C

#define ADF_GEN6_VFLNOTIFY	BIT(7)

/* Number of heartbeat counter pairs */
#define ADF_NUM_HB_CNT_PER_AE ADF_NUM_THREADS_PER_AE

/* Rate Limiting */
#define ADF_GEN6_RL_R2L_OFFSET			0x508000
#define ADF_GEN6_RL_L2C_OFFSET			0x509000
#define ADF_GEN6_RL_C2S_OFFSET			0x508818
#define ADF_GEN6_RL_TOKEN_PCIEIN_BUCKET_OFFSET	0x508800
#define ADF_GEN6_RL_TOKEN_PCIEOUT_BUCKET_OFFSET	0x508804

/* Physical function fuses */
#define ADF_6XXX_ACCELENGINES_MASK	GENMASK(8, 0)
#define ADF_6XXX_ADMIN_AE_MASK		GENMASK(8, 8)

/* Firmware binaries */
#define ADF_6XXX_FW		"qat_6xxx.bin"
#define ADF_6XXX_MMP		"qat_6xxx_mmp.bin"
#define ADF_6XXX_CY_OBJ		"qat_6xxx_cy.bin"
#define ADF_6XXX_DC_OBJ		"qat_6xxx_dc.bin"
#define ADF_6XXX_ADMIN_OBJ	"qat_6xxx_admin.bin"

/* RL constants */
#define ADF_6XXX_RL_PCIE_SCALE_FACTOR_DIV	100
#define ADF_6XXX_RL_PCIE_SCALE_FACTOR_MUL	102
#define ADF_6XXX_RL_SCANS_PER_SEC		954
#define ADF_6XXX_RL_MAX_TP_ASYM			173750UL
#define ADF_6XXX_RL_MAX_TP_SYM			95000UL
#define ADF_6XXX_RL_MAX_TP_DC			40000UL
#define ADF_6XXX_RL_MAX_TP_DECOMP		40000UL
#define ADF_6XXX_RL_SLICE_REF			1000UL

/* Clock frequency */
#define ADF_6XXX_AE_FREQ			(1000 * HZ_PER_MHZ)

enum icp_qat_gen6_slice_mask {
	ICP_ACCEL_GEN6_MASK_UCS_SLICE = BIT(0),
	ICP_ACCEL_GEN6_MASK_AUTH_SLICE = BIT(1),
	ICP_ACCEL_GEN6_MASK_PKE_SLICE = BIT(2),
	ICP_ACCEL_GEN6_MASK_CPR_SLICE = BIT(3),
	ICP_ACCEL_GEN6_MASK_DCPRZ_SLICE = BIT(4),
	ICP_ACCEL_GEN6_MASK_WCP_WAT_SLICE = BIT(6),
};

void adf_init_hw_data_6xxx(struct adf_hw_device_data *hw_data);
void adf_clean_hw_data_6xxx(struct adf_hw_device_data *hw_data);

#endif /* ADF_6XXX_HW_DATA_H_ */
