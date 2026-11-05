/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RZ/G3L LVDS Interface Registers Definitions
 *
 * Copyright (C) 2026 Renesas Electronics Corporation
 *
 */

#ifndef __RZG3L_LVDS_REGS_H__
#define __RZG3L_LVDS_REGS_H__

#define LVDS_CMN			0x00
#define LVDS_CMN_RST_PHY0_SEL		BIT(24)
#define LVDS_CMN_PHY_RESET		BIT(0)

#define LVDS_0_PHY_OFFSET		0x10
#define LVDS_0_PHY_CH_IO_EN0_MSK	0x1f
#define LVDS_0_PHY_CH_IO_EN0		(LVDS_0_PHY_CH_IO_EN0_MSK << 0)
#define LVDS_0_PHY_CH_EN_BGR		BIT(8)
#define LVDS_0_PHY_CH_EN_LDO		BIT(9)

#define LVDS_0_CTL_OFFSET		0x14
#define LVDS_0_CTL_FMT_SEL0_MSK		GENMASK(23, 20)

#endif /* __RZG3L_LVDS_REGS_H__ */
