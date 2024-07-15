/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#ifndef _UFS_MEDIATEK_SIP_H
#define _UFS_MEDIATEK_SIP_H

#include <linux/soc/mediatek/mtk_sip_svc.h>

/*
 * SiP (Slicon Partner) commands
 */
#define MTK_SIP_UFS_CONTROL               MTK_SIP_SMC_CMD(0x276)
#define UFS_MTK_SIP_VA09_PWR_CTRL         BIT(0)
#define UFS_MTK_SIP_DEVICE_RESET          BIT(1)
#define UFS_MTK_SIP_CRYPTO_CTRL           BIT(2)
#define UFS_MTK_SIP_REF_CLK_NOTIFICATION  BIT(3)
#define UFS_MTK_SIP_SRAM_PWR_CTRL         BIT(5)
#define UFS_MTK_SIP_GET_VCC_NUM           BIT(6)
#define UFS_MTK_SIP_DEVICE_PWR_CTRL       BIT(7)
#define UFS_MTK_SIP_MPHY_CTRL             BIT(8)
#define UFS_MTK_SIP_MTCMOS_CTRL           BIT(9)

/*
 * Multi-VCC by Numbering
 */
enum ufs_mtk_vcc_num {
	UFS_VCC_NONE = 0,
	UFS_VCC_1,
	UFS_VCC_2,
	UFS_VCC_MAX
};

enum ufs_mtk_mphy_op {
	UFS_MPHY_BACKUP = 0,
	UFS_MPHY_RESTORE
};

/*
 * SMC call wrapper function
 */
struct ufs_mtk_smc_arg {
	unsigned long cmd;
	struct arm_smccc_res *res;
	unsigned long v1;
	unsigned long v2;
	unsigned long v3;
	unsigned long v4;
	unsigned long v5;
	unsigned long v6;
	unsigned long v7;
};


static inline void _ufs_mtk_smc(struct ufs_mtk_smc_arg s)
{
	arm_smccc_smc(MTK_SIP_UFS_CONTROL,
		s.cmd,
		s.v1, s.v2, s.v3, s.v4, s.v5, s.v6, s.res);
}

#define ufs_mtk_smc(...) \
	_ufs_mtk_smc((struct ufs_mtk_smc_arg) {__VA_ARGS__})

/* Sip kernel interface */
#define ufs_mtk_va09_pwr_ctrl(res, on) \
	ufs_mtk_smc(UFS_MTK_SIP_VA09_PWR_CTRL, &(res), on)

#define ufs_mtk_crypto_ctrl(res, enable) \
	ufs_mtk_smc(UFS_MTK_SIP_CRYPTO_CTRL, &(res), enable)

#define ufs_mtk_ref_clk_notify(on, stage, res) \
	ufs_mtk_smc(UFS_MTK_SIP_REF_CLK_NOTIFICATION, &(res), on, stage)

#define ufs_mtk_device_reset_ctrl(high, res) \
	ufs_mtk_smc(UFS_MTK_SIP_DEVICE_RESET, &(res), high)

#define ufs_mtk_sram_pwr_ctrl(on, res) \
	ufs_mtk_smc(UFS_MTK_SIP_SRAM_PWR_CTRL, &(res), on)

#define ufs_mtk_get_vcc_num(res) \
	ufs_mtk_smc(UFS_MTK_SIP_GET_VCC_NUM, &(res))

#define ufs_mtk_device_pwr_ctrl(on, ufs_version, res) \
	ufs_mtk_smc(UFS_MTK_SIP_DEVICE_PWR_CTRL, &(res), on, ufs_version)

#define ufs_mtk_mphy_ctrl(op, res) \
	ufs_mtk_smc(UFS_MTK_SIP_MPHY_CTRL, &(res), op)

#define ufs_mtk_mtcmos_ctrl(op, res) \
	ufs_mtk_smc(UFS_MTK_SIP_MTCMOS_CTRL, &(res), op)

#endif /* !_UFS_MEDIATEK_SIP_H */
