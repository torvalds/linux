/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023, Intel Corporation
 * stmmac EST(802.3 Qbv) handling
 */

#define EST_GMAC4_OFFSET		0x00000c50
#define EST_XGMAC_OFFSET		0x00001050

#define EST_CONTROL			0x00000000
#define EST_GMAC5_PTOV			GENMASK(31, 24)
#define EST_GMAC5_PTOV_SHIFT		24
#define EST_GMAC5_PTOV_MUL		6
#define EST_XGMAC_PTOV			GENMASK(31, 23)
#define EST_XGMAC_PTOV_SHIFT		23
#define EST_XGMAC_PTOV_MUL		9
#define EST_SSWL			BIT(1)
#define EST_EEST			BIT(0)
#define EST_DFBS			BIT(5)

#define EST_STATUS			0x00000008
#define EST_GMAC5_BTRL			GENMASK(11, 8)
#define EST_XGMAC_BTRL			GENMASK(15, 8)
#define EST_SWOL			BIT(7)
#define EST_SWOL_SHIFT			7
#define EST_CGCE			BIT(4)
#define EST_HLBS			BIT(3)
#define EST_HLBF			BIT(2)
#define EST_BTRE			BIT(1)
#define EST_SWLC			BIT(0)

#define EST_SCH_ERR			0x00000010

#define EST_FRM_SZ_ERR			0x00000014

#define EST_FRM_SZ_CAP			0x00000018
#define EST_SZ_CAP_HBFS_MASK		GENMASK(14, 0)
#define EST_SZ_CAP_HBFQ_SHIFT		16
#define EST_SZ_CAP_HBFQ_MASK(val)		\
	({					\
		typeof(val) _val = (val);	\
		(_val > 4 ? GENMASK(18, 16) :	\
		 _val > 2 ? GENMASK(17, 16) :	\
		 BIT(16));			\
	})

#define EST_INT_EN			0x00000020
#define EST_IECGCE			EST_CGCE
#define EST_IEHS			EST_HLBS
#define EST_IEHF			EST_HLBF
#define EST_IEBE			EST_BTRE
#define EST_IECC			EST_SWLC

#define EST_GCL_CONTROL			0x00000030
#define EST_BTR_LOW			0x0
#define EST_BTR_HIGH			0x1
#define EST_CTR_LOW			0x2
#define EST_CTR_HIGH			0x3
#define EST_TER				0x4
#define EST_LLR				0x5
#define EST_ADDR_SHIFT			8
#define EST_GCRR			BIT(2)
#define EST_SRWO			BIT(0)

#define EST_GCL_DATA			0x00000034

extern const struct stmmac_est_ops dwmac510_est_ops;
