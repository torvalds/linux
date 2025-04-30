/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2020 Intel Corporation */
#ifndef ADF_GEN2_HW_DATA_H_
#define ADF_GEN2_HW_DATA_H_

#include "adf_accel_devices.h"
#include "adf_cfg_common.h"

#define ADF_GEN2_RX_RINGS_OFFSET	8
#define ADF_GEN2_TX_RINGS_MASK		0xFF

/* AE to function map */
#define AE2FUNCTION_MAP_A_OFFSET	(0x3A400 + 0x190)
#define AE2FUNCTION_MAP_B_OFFSET	(0x3A400 + 0x310)
#define AE2FUNCTION_MAP_REG_SIZE	4
#define AE2FUNCTION_MAP_VALID		BIT(7)

#define READ_CSR_AE2FUNCTION_MAP_A(pmisc_bar_addr, index) \
	ADF_CSR_RD(pmisc_bar_addr, AE2FUNCTION_MAP_A_OFFSET + \
		   AE2FUNCTION_MAP_REG_SIZE * (index))
#define WRITE_CSR_AE2FUNCTION_MAP_A(pmisc_bar_addr, index, value) \
	ADF_CSR_WR(pmisc_bar_addr, AE2FUNCTION_MAP_A_OFFSET + \
		   AE2FUNCTION_MAP_REG_SIZE * (index), value)
#define READ_CSR_AE2FUNCTION_MAP_B(pmisc_bar_addr, index) \
	ADF_CSR_RD(pmisc_bar_addr, AE2FUNCTION_MAP_B_OFFSET + \
		   AE2FUNCTION_MAP_REG_SIZE * (index))
#define WRITE_CSR_AE2FUNCTION_MAP_B(pmisc_bar_addr, index, value) \
	ADF_CSR_WR(pmisc_bar_addr, AE2FUNCTION_MAP_B_OFFSET + \
		   AE2FUNCTION_MAP_REG_SIZE * (index), value)

/* Admin Interface Offsets */
#define ADF_ADMINMSGUR_OFFSET	(0x3A000 + 0x574)
#define ADF_ADMINMSGLR_OFFSET	(0x3A000 + 0x578)
#define ADF_MAILBOX_BASE_OFFSET	0x20970

/* Arbiter configuration */
#define ADF_ARB_OFFSET			0x30000
#define ADF_ARB_WRK_2_SER_MAP_OFFSET	0x180
#define ADF_ARB_CONFIG			(BIT(31) | BIT(6) | BIT(0))

/* Power gating */
#define ADF_POWERGATE_DC		BIT(23)
#define ADF_POWERGATE_PKE		BIT(24)

/* Default ring mapping */
#define ADF_GEN2_DEFAULT_RING_TO_SRV_MAP \
	(CRYPTO << ADF_CFG_SERV_RING_PAIR_0_SHIFT | \
	 CRYPTO << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	 UNUSED << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	   COMP << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

/* WDT timers
 *
 * Timeout is in cycles. Clock speed may vary across products but this
 * value should be a few milli-seconds.
 */
#define ADF_SSM_WDT_DEFAULT_VALUE	0x200000
#define ADF_SSM_WDT_PKE_DEFAULT_VALUE	0x2000000
#define ADF_SSMWDT_OFFSET		0x54
#define ADF_SSMWDTPKE_OFFSET		0x58
#define ADF_SSMWDT(i)		(ADF_SSMWDT_OFFSET + ((i) * 0x4000))
#define ADF_SSMWDTPKE(i)	(ADF_SSMWDTPKE_OFFSET + ((i) * 0x4000))

/* Error detection and correction */
#define ADF_GEN2_AE_CTX_ENABLES(i)	((i) * 0x1000 + 0x20818)
#define ADF_GEN2_AE_MISC_CONTROL(i)	((i) * 0x1000 + 0x20960)
#define ADF_GEN2_ENABLE_AE_ECC_ERR	BIT(28)
#define ADF_GEN2_ENABLE_AE_ECC_PARITY_CORR	(BIT(24) | BIT(12))
#define ADF_GEN2_UERRSSMSH(i)		((i) * 0x4000 + 0x18)
#define ADF_GEN2_CERRSSMSH(i)		((i) * 0x4000 + 0x10)
#define ADF_GEN2_ERRSSMSH_EN		BIT(3)

/* Number of heartbeat counter pairs */
#define ADF_NUM_HB_CNT_PER_AE ADF_NUM_THREADS_PER_AE

/* Interrupts */
#define ADF_GEN2_SMIAPF0_MASK_OFFSET    (0x3A000 + 0x28)
#define ADF_GEN2_SMIAPF1_MASK_OFFSET    (0x3A000 + 0x30)
#define ADF_GEN2_SMIA1_MASK             0x1

u32 adf_gen2_get_num_accels(struct adf_hw_device_data *self);
u32 adf_gen2_get_num_aes(struct adf_hw_device_data *self);
void adf_gen2_enable_error_correction(struct adf_accel_dev *accel_dev);
void adf_gen2_cfg_iov_thds(struct adf_accel_dev *accel_dev, bool enable,
			   int num_a_regs, int num_b_regs);
void adf_gen2_get_admin_info(struct admin_info *admin_csrs_info);
void adf_gen2_get_arb_info(struct arb_info *arb_info);
void adf_gen2_enable_ints(struct adf_accel_dev *accel_dev);
u32 adf_gen2_get_accel_cap(struct adf_accel_dev *accel_dev);
void adf_gen2_set_ssm_wdtimer(struct adf_accel_dev *accel_dev);
void adf_gen2_init_dc_ops(struct adf_dc_ops *dc_ops);

#endif
