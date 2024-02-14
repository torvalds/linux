/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * UNISOC UFS Host Controller driver
 *
 * Copyright (C) 2022 Unisoc, Inc.
 * Author: Zhe Wang <zhe.wang1@unisoc.com>
 */

#ifndef _UFS_SPRD_H_
#define _UFS_SPRD_H_

/* Vendor specific attributes */
#define RXSQCONTROL	0x8009
#define CBRATESEL	0x8114
#define CBCREGADDRLSB	0x8116
#define CBCREGADDRMSB	0x8117
#define CBCREGWRLSB	0x8118
#define CBCREGWRMSB	0x8119
#define CBCREGRDWRSEL	0x811C
#define CBCRCTRL	0x811F
#define CBREFCLKCTRL2	0x8132
#define VS_MPHYDISABLE	0xD0C1

#define APB_UFSDEV_REG		0xCE8
#define APB_UFSDEV_REFCLK_EN	0x2
#define APB_USB31PLL_CTRL	0xCFC
#define APB_USB31PLLV_REF2MPHY	0x1

#define SPRD_SIP_SVC_STORAGE_UFS_CRYPTO_ENABLE				\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_32,				\
			   ARM_SMCCC_OWNER_SIP,				\
			   0x0301)

enum SPRD_UFS_RST_INDEX {
	SPRD_UFSHCI_SOFT_RST,
	SPRD_UFS_DEV_RST,

	SPRD_UFS_RST_MAX
};

enum SPRD_UFS_SYSCON_INDEX {
	SPRD_UFS_ANLG,
	SPRD_UFS_AON_APB,

	SPRD_UFS_SYSCON_MAX
};

enum SPRD_UFS_VREG_INDEX {
	SPRD_UFS_VDD_MPHY,

	SPRD_UFS_VREG_MAX
};

struct ufs_sprd_rst {
	const char *name;
	struct reset_control *rc;
};

struct ufs_sprd_syscon {
	const char *name;
	struct regmap *regmap;
};

struct ufs_sprd_vreg {
	const char *name;
	struct regulator *vreg;
};

struct ufs_sprd_priv {
	struct ufs_sprd_rst rci[SPRD_UFS_RST_MAX];
	struct ufs_sprd_syscon sysci[SPRD_UFS_SYSCON_MAX];
	struct ufs_sprd_vreg vregi[SPRD_UFS_VREG_MAX];
	const struct ufs_hba_variant_ops ufs_hba_sprd_vops;
};

struct ufs_sprd_host {
	struct ufs_hba *hba;
	struct ufs_sprd_priv *priv;
	void __iomem *ufs_dbg_mmio;

	enum ufs_unipro_ver unipro_ver;
};

#endif /* _UFS_SPRD_H_ */
