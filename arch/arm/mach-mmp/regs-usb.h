/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2011 Marvell International Ltd. All rights reserved.
 */

#ifndef __ASM_ARCH_REGS_USB_H
#define __ASM_ARCH_REGS_USB_H

#define PXA168_U2O_REGBASE	(0xd4208000)
#define PXA168_U2O_PHYBASE	(0xd4207000)

#define PXA168_U2H_REGBASE      (0xd4209000)
#define PXA168_U2H_PHYBASE      (0xd4206000)

#define MMP3_HSIC1_REGBASE	(0xf0001000)
#define MMP3_HSIC1_PHYBASE	(0xf0001800)

#define MMP3_HSIC2_REGBASE	(0xf0002000)
#define MMP3_HSIC2_PHYBASE	(0xf0002800)

#define MMP3_FSIC_REGBASE	(0xf0003000)
#define MMP3_FSIC_PHYBASE	(0xf0003800)


#define USB_REG_RANGE		(0x1ff)
#define USB_PHY_RANGE		(0xff)

/* registers */
#define U2x_CAPREGS_OFFSET       0x100

/* phy regs */
#define UTMI_REVISION		0x0
#define UTMI_CTRL		0x4
#define UTMI_PLL		0x8
#define UTMI_TX			0xc
#define UTMI_RX			0x10
#define UTMI_IVREF		0x14
#define UTMI_T0			0x18
#define UTMI_T1			0x1c
#define UTMI_T2			0x20
#define UTMI_T3			0x24
#define UTMI_T4			0x28
#define UTMI_T5			0x2c
#define UTMI_RESERVE		0x30
#define UTMI_USB_INT		0x34
#define UTMI_DBG_CTL		0x38
#define UTMI_OTG_ADDON		0x3c

/* For UTMICTRL Register */
#define UTMI_CTRL_USB_CLK_EN                    (1 << 31)
/* pxa168 */
#define UTMI_CTRL_SUSPEND_SET1                  (1 << 30)
#define UTMI_CTRL_SUSPEND_SET2                  (1 << 29)
#define UTMI_CTRL_RXBUF_PDWN                    (1 << 24)
#define UTMI_CTRL_TXBUF_PDWN                    (1 << 11)

#define UTMI_CTRL_INPKT_DELAY_SHIFT             30
#define UTMI_CTRL_INPKT_DELAY_SOF_SHIFT		28
#define UTMI_CTRL_PU_REF_SHIFT			20
#define UTMI_CTRL_ARC_PULLDN_SHIFT              12
#define UTMI_CTRL_PLL_PWR_UP_SHIFT              1
#define UTMI_CTRL_PWR_UP_SHIFT                  0

/* For UTMI_PLL Register */
#define UTMI_PLL_PLLCALI12_SHIFT		29
#define UTMI_PLL_PLLCALI12_MASK			(0x3 << 29)

#define UTMI_PLL_PLLVDD18_SHIFT			27
#define UTMI_PLL_PLLVDD18_MASK			(0x3 << 27)

#define UTMI_PLL_PLLVDD12_SHIFT			25
#define UTMI_PLL_PLLVDD12_MASK			(0x3 << 25)

#define UTMI_PLL_CLK_BLK_EN_SHIFT               24
#define CLK_BLK_EN                              (0x1 << 24)
#define PLL_READY                               (0x1 << 23)
#define KVCO_EXT                                (0x1 << 22)
#define VCOCAL_START                            (0x1 << 21)

#define UTMI_PLL_KVCO_SHIFT			15
#define UTMI_PLL_KVCO_MASK                      (0x7 << 15)

#define UTMI_PLL_ICP_SHIFT			12
#define UTMI_PLL_ICP_MASK                       (0x7 << 12)

#define UTMI_PLL_FBDIV_SHIFT                    4
#define UTMI_PLL_FBDIV_MASK                     (0xFF << 4)

#define UTMI_PLL_REFDIV_SHIFT                   0
#define UTMI_PLL_REFDIV_MASK                    (0xF << 0)

/* For UTMI_TX Register */
#define UTMI_TX_REG_EXT_FS_RCAL_SHIFT		27
#define UTMI_TX_REG_EXT_FS_RCAL_MASK		(0xf << 27)

#define UTMI_TX_REG_EXT_FS_RCAL_EN_SHIFT	26
#define UTMI_TX_REG_EXT_FS_RCAL_EN_MASK		(0x1 << 26)

#define UTMI_TX_TXVDD12_SHIFT                   22
#define UTMI_TX_TXVDD12_MASK                    (0x3 << 22)

#define UTMI_TX_CK60_PHSEL_SHIFT                17
#define UTMI_TX_CK60_PHSEL_MASK                 (0xf << 17)

#define UTMI_TX_IMPCAL_VTH_SHIFT                14
#define UTMI_TX_IMPCAL_VTH_MASK                 (0x7 << 14)

#define REG_RCAL_START                          (0x1 << 12)

#define UTMI_TX_LOW_VDD_EN_SHIFT                11

#define UTMI_TX_AMP_SHIFT			0
#define UTMI_TX_AMP_MASK			(0x7 << 0)

/* For UTMI_RX Register */
#define UTMI_REG_SQ_LENGTH_SHIFT                15
#define UTMI_REG_SQ_LENGTH_MASK                 (0x3 << 15)

#define UTMI_RX_SQ_THRESH_SHIFT                 4
#define UTMI_RX_SQ_THRESH_MASK                  (0xf << 4)

#define UTMI_OTG_ADDON_OTG_ON			(1 << 0)

/* fsic registers */
#define FSIC_MISC			0x4
#define FSIC_INT			0x28
#define FSIC_CTRL			0x30

/* HSIC registers */
#define HSIC_PAD_CTRL			0x4

#define HSIC_CTRL			0x8
#define HSIC_CTRL_HSIC_ENABLE		(1<<7)
#define HSIC_CTRL_PLL_BYPASS		(1<<4)

#define TEST_GRP_0			0xc
#define TEST_GRP_1			0x10

#define HSIC_INT			0x14
#define HSIC_INT_READY_INT_EN		(1<<10)
#define HSIC_INT_CONNECT_INT_EN		(1<<9)
#define HSIC_INT_CORE_INT_EN		(1<<8)
#define HSIC_INT_HS_READY		(1<<2)
#define HSIC_INT_CONNECT		(1<<1)
#define HSIC_INT_CORE			(1<<0)

#define HSIC_CONFIG			0x18
#define USBHSIC_CTRL			0x20

#define HSIC_USB_CTRL			0x28
#define HSIC_USB_CTRL_CLKEN		1
#define	HSIC_USB_CLK_PHY		0x0
#define HSIC_USB_CLK_PMU		0x1

#endif /* __ASM_ARCH_PXA_U2O_H */
