/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2013 - 2015 Fujitsu Semiconductor, Ltd
 *              Vincent Yang <vincent.yang@tw.fujitsu.com>
 * Copyright (C) 2015 Linaro Ltd  Andy Green <andy.green@linaro.org>
 * Copyright (C) 2019 Socionext Inc.
 *
 */

/* F_SDH30 extended Controller registers */
#define F_SDH30_AHB_CONFIG      0x100
#define  F_SDH30_AHB_BIGED      BIT(6)
#define  F_SDH30_BUSLOCK_DMA    BIT(5)
#define  F_SDH30_BUSLOCK_EN     BIT(4)
#define  F_SDH30_SIN            BIT(3)
#define  F_SDH30_AHB_INCR_16    BIT(2)
#define  F_SDH30_AHB_INCR_8     BIT(1)
#define  F_SDH30_AHB_INCR_4     BIT(0)

#define F_SDH30_TUNING_SETTING  0x108
#define  F_SDH30_CMD_CHK_DIS    BIT(16)

#define F_SDH30_IO_CONTROL2     0x114
#define  F_SDH30_CRES_O_DN      BIT(19)
#define  F_SDH30_MSEL_O_1_8     BIT(18)

#define F_SDH30_ESD_CONTROL     0x124
#define	 F_SDH30_EMMC_RST		BIT(1)
#define  F_SDH30_CMD_DAT_DELAY	BIT(9)
#define	 F_SDH30_EMMC_HS200		BIT(24)

#define F_SDH30_MIN_CLOCK		400000
