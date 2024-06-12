/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2020 Intel Corporation */
#ifndef ADF_GEN4_HW_DATA_H_
#define ADF_GEN4_HW_DATA_H_

#include <linux/units.h>

#include "adf_accel_devices.h"
#include "adf_cfg_common.h"

/* PCIe configuration space */
#define ADF_GEN4_BAR_MASK	(BIT(0) | BIT(2) | BIT(4))
#define ADF_GEN4_SRAM_BAR	0
#define ADF_GEN4_PMISC_BAR	1
#define ADF_GEN4_ETR_BAR	2

/* Clocks frequency */
#define ADF_GEN4_KPT_COUNTER_FREQ	(100 * HZ_PER_MHZ)

/* Physical function fuses */
#define ADF_GEN4_FUSECTL0_OFFSET	0x2C8
#define ADF_GEN4_FUSECTL1_OFFSET	0x2CC
#define ADF_GEN4_FUSECTL2_OFFSET	0x2D0
#define ADF_GEN4_FUSECTL3_OFFSET	0x2D4
#define ADF_GEN4_FUSECTL4_OFFSET	0x2D8
#define ADF_GEN4_FUSECTL5_OFFSET	0x2DC

/* Accelerators */
#define ADF_GEN4_ACCELERATORS_MASK	0x1
#define ADF_GEN4_MAX_ACCELERATORS	1
#define ADF_GEN4_ADMIN_ACCELENGINES	1

/* MSIX interrupt */
#define ADF_GEN4_SMIAPF_RP_X0_MASK_OFFSET	0x41A040
#define ADF_GEN4_SMIAPF_RP_X1_MASK_OFFSET	0x41A044
#define ADF_GEN4_SMIAPF_MASK_OFFSET		0x41A084
#define ADF_GEN4_MSIX_RTTABLE_OFFSET(i)		(0x409000 + ((i) * 0x04))

/* Bank and ring configuration */
#define ADF_GEN4_MAX_RPS		64
#define ADF_GEN4_NUM_RINGS_PER_BANK	2
#define ADF_GEN4_NUM_BANKS_PER_VF	4
#define ADF_GEN4_ETR_MAX_BANKS		64
#define ADF_GEN4_RX_RINGS_OFFSET	1
#define ADF_GEN4_TX_RINGS_MASK		0x1

/* Arbiter configuration */
#define ADF_GEN4_ARB_CONFIG			(BIT(31) | BIT(6) | BIT(0))
#define ADF_GEN4_ARB_OFFSET			0x0
#define ADF_GEN4_ARB_WRK_2_SER_MAP_OFFSET	0x400

/* Admin Interface Reg Offset */
#define ADF_GEN4_ADMINMSGUR_OFFSET	0x500574
#define ADF_GEN4_ADMINMSGLR_OFFSET	0x500578
#define ADF_GEN4_MAILBOX_BASE_OFFSET	0x600970

/* Default ring mapping */
#define ADF_GEN4_DEFAULT_RING_TO_SRV_MAP \
	(ASYM << ADF_CFG_SERV_RING_PAIR_0_SHIFT | \
	  SYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	 ASYM << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	  SYM << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

/* WDT timers
 *
 * Timeout is in cycles. Clock speed may vary across products but this
 * value should be a few milli-seconds.
 */
#define ADF_SSM_WDT_DEFAULT_VALUE	0x7000000ULL
#define ADF_SSM_WDT_PKE_DEFAULT_VALUE	0x8000000
#define ADF_SSMWDTL_OFFSET		0x54
#define ADF_SSMWDTH_OFFSET		0x5C
#define ADF_SSMWDTPKEL_OFFSET		0x58
#define ADF_SSMWDTPKEH_OFFSET		0x60

/* Ring reset */
#define ADF_RPRESET_POLL_TIMEOUT_US	(5 * USEC_PER_SEC)
#define ADF_RPRESET_POLL_DELAY_US	20
#define ADF_WQM_CSR_RPRESETCTL_RESET	BIT(0)
#define ADF_WQM_CSR_RPRESETCTL_DRAIN	BIT(2)
#define ADF_WQM_CSR_RPRESETCTL(bank)	(0x6000 + ((bank) << 3))
#define ADF_WQM_CSR_RPRESETSTS_STATUS	BIT(0)
#define ADF_WQM_CSR_RPRESETSTS(bank)	(ADF_WQM_CSR_RPRESETCTL(bank) + 4)

/* Ring interrupt */
#define ADF_RP_INT_SRC_SEL_F_RISE_MASK	BIT(2)
#define ADF_RP_INT_SRC_SEL_F_FALL_MASK	GENMASK(2, 0)
#define ADF_RP_INT_SRC_SEL_RANGE_WIDTH	4
#define ADF_COALESCED_POLL_TIMEOUT_US	(1 * USEC_PER_SEC)
#define ADF_COALESCED_POLL_DELAY_US	1000
#define ADF_WQM_CSR_RPINTSOU(bank)	(0x200000 + ((bank) << 12))
#define ADF_WQM_CSR_RP_IDX_RX		1

/* Error source registers */
#define ADF_GEN4_ERRSOU0	(0x41A200)
#define ADF_GEN4_ERRSOU1	(0x41A204)
#define ADF_GEN4_ERRSOU2	(0x41A208)
#define ADF_GEN4_ERRSOU3	(0x41A20C)

/* Error source mask registers */
#define ADF_GEN4_ERRMSK0	(0x41A210)
#define ADF_GEN4_ERRMSK1	(0x41A214)
#define ADF_GEN4_ERRMSK2	(0x41A218)
#define ADF_GEN4_ERRMSK3	(0x41A21C)

#define ADF_GEN4_VFLNOTIFY	BIT(7)

/* Number of heartbeat counter pairs */
#define ADF_NUM_HB_CNT_PER_AE ADF_NUM_THREADS_PER_AE

/* Rate Limiting */
#define ADF_GEN4_RL_R2L_OFFSET			0x508000
#define ADF_GEN4_RL_L2C_OFFSET			0x509000
#define ADF_GEN4_RL_C2S_OFFSET			0x508818
#define ADF_GEN4_RL_TOKEN_PCIEIN_BUCKET_OFFSET	0x508800
#define ADF_GEN4_RL_TOKEN_PCIEOUT_BUCKET_OFFSET	0x508804

/* Arbiter threads mask with error value */
#define ADF_GEN4_ENA_THD_MASK_ERROR	GENMASK(ADF_NUM_THREADS_PER_AE, 0)

/* PF2VM communication channel */
#define ADF_GEN4_PF2VM_OFFSET(i)	(0x40B010 + (i) * 0x20)
#define ADF_GEN4_VM2PF_OFFSET(i)	(0x40B014 + (i) * 0x20)
#define ADF_GEN4_VINTMSKPF2VM_OFFSET(i)	(0x40B00C + (i) * 0x20)
#define ADF_GEN4_VINTSOUPF2VM_OFFSET(i)	(0x40B008 + (i) * 0x20)
#define ADF_GEN4_VINTMSK_OFFSET(i)	(0x40B004 + (i) * 0x20)
#define ADF_GEN4_VINTSOU_OFFSET(i)	(0x40B000 + (i) * 0x20)

struct adf_gen4_vfmig {
	struct adf_mstate_mgr *mstate_mgr;
	bool bank_stopped[ADF_GEN4_NUM_BANKS_PER_VF];
};

void adf_gen4_set_ssm_wdtimer(struct adf_accel_dev *accel_dev);

enum icp_qat_gen4_slice_mask {
	ICP_ACCEL_GEN4_MASK_CIPHER_SLICE = BIT(0),
	ICP_ACCEL_GEN4_MASK_AUTH_SLICE = BIT(1),
	ICP_ACCEL_GEN4_MASK_PKE_SLICE = BIT(2),
	ICP_ACCEL_GEN4_MASK_COMPRESS_SLICE = BIT(3),
	ICP_ACCEL_GEN4_MASK_UCS_SLICE = BIT(4),
	ICP_ACCEL_GEN4_MASK_EIA3_SLICE = BIT(5),
	ICP_ACCEL_GEN4_MASK_SMX_SLICE = BIT(7),
	ICP_ACCEL_GEN4_MASK_WCP_WAT_SLICE = BIT(8),
	ICP_ACCEL_GEN4_MASK_ZUC_256_SLICE = BIT(9),
};

enum adf_gen4_rp_groups {
	RP_GROUP_0,
	RP_GROUP_1,
	RP_GROUP_COUNT
};

void adf_gen4_enable_error_correction(struct adf_accel_dev *accel_dev);
void adf_gen4_enable_ints(struct adf_accel_dev *accel_dev);
u32 adf_gen4_get_accel_mask(struct adf_hw_device_data *self);
void adf_gen4_get_admin_info(struct admin_info *admin_csrs_info);
void adf_gen4_get_arb_info(struct arb_info *arb_info);
u32 adf_gen4_get_etr_bar_id(struct adf_hw_device_data *self);
u32 adf_gen4_get_heartbeat_clock(struct adf_hw_device_data *self);
u32 adf_gen4_get_misc_bar_id(struct adf_hw_device_data *self);
u32 adf_gen4_get_num_accels(struct adf_hw_device_data *self);
u32 adf_gen4_get_num_aes(struct adf_hw_device_data *self);
enum dev_sku_info adf_gen4_get_sku(struct adf_hw_device_data *self);
u32 adf_gen4_get_sram_bar_id(struct adf_hw_device_data *self);
int adf_gen4_init_device(struct adf_accel_dev *accel_dev);
int adf_gen4_ring_pair_reset(struct adf_accel_dev *accel_dev, u32 bank_number);
void adf_gen4_set_msix_default_rttable(struct adf_accel_dev *accel_dev);
void adf_gen4_set_ssm_wdtimer(struct adf_accel_dev *accel_dev);
int adf_gen4_init_thd2arb_map(struct adf_accel_dev *accel_dev);
u16 adf_gen4_get_ring_to_svc_map(struct adf_accel_dev *accel_dev);
int adf_gen4_bank_quiesce_coal_timer(struct adf_accel_dev *accel_dev,
				     u32 bank_idx, int timeout_ms);
int adf_gen4_bank_drain_start(struct adf_accel_dev *accel_dev,
			      u32 bank_number, int timeout_us);
void adf_gen4_bank_drain_finish(struct adf_accel_dev *accel_dev,
				u32 bank_number);
int adf_gen4_bank_state_save(struct adf_accel_dev *accel_dev, u32 bank_number,
			     struct bank_state *state);
int adf_gen4_bank_state_restore(struct adf_accel_dev *accel_dev,
				u32 bank_number, struct bank_state *state);

#endif
