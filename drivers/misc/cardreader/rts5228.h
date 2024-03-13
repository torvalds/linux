/* SPDX-License-Identifier: GPL-2.0-only */
/* Driver for Realtek PCI-Express card reader
 *
 * Copyright(c) 2018-2019 Realtek Semiconductor Corp. All rights reserved.
 *
 * Author:
 *   Ricky WU <ricky_wu@realtek.com>
 *   Rui FENG <rui_feng@realsil.com.cn>
 *   Wei WANG <wei_wang@realsil.com.cn>
 */
#ifndef RTS5228_H
#define RTS5228_H


#define RTS5228_AUTOLOAD_CFG0			0xFF7B
#define RTS5228_AUTOLOAD_CFG1			0xFF7C
#define RTS5228_AUTOLOAD_CFG2			0xFF7D
#define RTS5228_AUTOLOAD_CFG3			0xFF7E
#define RTS5228_AUTOLOAD_CFG4			0xFF7F

#define RTS5228_REG_VREF			0xFE97
#define RTS5228_PWD_SUSPND_EN			(1 << 4)

#define RTS5228_PAD_H3L1			0xFF79
#define PAD_GPIO_H3L1			(1 << 3)

/* SSC_CTL2 0xFC12 */
#define RTS5228_SSC_DEPTH_MASK		0x07
#define RTS5228_SSC_DEPTH_DISALBE	0x00
#define RTS5228_SSC_DEPTH_8M		0x01
#define RTS5228_SSC_DEPTH_4M		0x02
#define RTS5228_SSC_DEPTH_2M		0x03
#define RTS5228_SSC_DEPTH_1M		0x04
#define RTS5228_SSC_DEPTH_512K		0x05
#define RTS5228_SSC_DEPTH_256K		0x06
#define RTS5228_SSC_DEPTH_128K		0x07

/* DMACTL 0xFE2C */
#define RTS5228_DMA_PACK_SIZE_MASK		0xF0

#define RTS5228_REG_LDO12_CFG			0xFF6E
#define RTS5228_LDO12_VO_TUNE_MASK		(0x07<<1)
#define RTS5228_LDO12_100				(0x00<<1)
#define RTS5228_LDO12_105				(0x01<<1)
#define RTS5228_LDO12_110				(0x02<<1)
#define RTS5228_LDO12_115				(0x03<<1)
#define RTS5228_LDO12_120				(0x04<<1)
#define RTS5228_LDO12_125				(0x05<<1)
#define RTS5228_LDO12_130				(0x06<<1)
#define RTS5228_LDO12_135				(0x07<<1)
#define RTS5228_REG_PWD_LDO12			(0x01<<0)

#define RTS5228_REG_LDO12_L12	0xFF6F
#define RTS5228_LDO12_L12_MASK			(0x07<<4)
#define RTS5228_LDO12_L12_120			(0x04<<4)

/* LDO control register */
#define RTS5228_CARD_PWR_CTL			0xFD50
#define RTS5228_PUPDC					(0x01<<5)

#define RTS5228_LDO1233318_POW_CTL		0xFF70
#define RTS5228_LDO3318_POWERON			(0x01<<3)
#define RTS5228_LDO1_POWEROFF			(0x00<<0)
#define RTS5228_LDO1_SOFTSTART			(0x01<<0)
#define RTS5228_LDO1_FULLON				(0x03<<0)
#define RTS5228_LDO1_POWERON_MASK		(0x03<<0)
#define RTS5228_LDO_POWERON_MASK		(0x0F<<0)

#define RTS5228_DV3318_CFG				0xFF71
#define RTS5228_DV3318_TUNE_MASK		(0x07<<4)
#define RTS5228_DV3318_17				(0x00<<4)
#define RTS5228_DV3318_1V75				(0x01<<4)
#define RTS5228_DV3318_18				(0x02<<4)
#define RTS5228_DV3318_1V85				(0x03<<4)
#define RTS5228_DV3318_19				(0x04<<4)
#define RTS5228_DV3318_33				(0x07<<4)
#define RTS5228_DV3318_SR_MASK			(0x03<<2)
#define RTS5228_DV3318_SR_0				(0x00<<2)
#define RTS5228_DV3318_SR_250			(0x01<<2)
#define RTS5228_DV3318_SR_500			(0x02<<2)
#define RTS5228_DV3318_SR_1000			(0x03<<2)

#define RTS5228_LDO1_CFG0				0xFF72
#define RTS5228_LDO1_OCP_THD_MASK		(0x07<<5)
#define RTS5228_LDO1_OCP_EN				(0x01<<4)
#define RTS5228_LDO1_OCP_LMT_THD_MASK	(0x03<<2)
#define RTS5228_LDO1_OCP_LMT_EN			(0x01<<1)

#define RTS5228_LDO1_OCP_THD_730		(0x00<<5)
#define RTS5228_LDO1_OCP_THD_780		(0x01<<5)
#define RTS5228_LDO1_OCP_THD_860		(0x02<<5)
#define RTS5228_LDO1_OCP_THD_930		(0x03<<5)
#define RTS5228_LDO1_OCP_THD_1000		(0x04<<5)
#define RTS5228_LDO1_OCP_THD_1070		(0x05<<5)
#define RTS5228_LDO1_OCP_THD_1140		(0x06<<5)
#define RTS5228_LDO1_OCP_THD_1220		(0x07<<5)

#define RTS5228_LDO1_LMT_THD_450		(0x00<<2)
#define RTS5228_LDO1_LMT_THD_1000		(0x01<<2)
#define RTS5228_LDO1_LMT_THD_1500		(0x02<<2)
#define RTS5228_LDO1_LMT_THD_2000		(0x03<<2)

#define RTS5228_LDO1_CFG1				0xFF73
#define RTS5228_LDO1_SR_TIME_MASK		(0x03<<6)
#define RTS5228_LDO1_SR_0_0				(0x00<<6)
#define RTS5228_LDO1_SR_0_25			(0x01<<6)
#define RTS5228_LDO1_SR_0_5				(0x02<<6)
#define RTS5228_LDO1_SR_1_0				(0x03<<6)
#define RTS5228_LDO1_TUNE_MASK			(0x07<<1)
#define RTS5228_LDO1_18					(0x05<<1)
#define RTS5228_LDO1_33					(0x07<<1)
#define RTS5228_LDO1_PWD_MASK			(0x01<<0)

#define RTS5228_AUXCLK_GAT_CTL			0xFF74

#define RTS5228_REG_RREF_CTL_0			0xFF75
#define RTS5228_FORCE_RREF_EXTL			(0x01<<7)
#define RTS5228_REG_BG33_MASK			(0x07<<0)
#define RTS5228_RREF_12_1V				(0x04<<0)
#define RTS5228_RREF_12_3V				(0x05<<0)

#define RTS5228_REG_RREF_CTL_1			0xFF76

#define RTS5228_REG_RREF_CTL_2			0xFF77
#define RTS5228_TEST_INTL_RREF			(0x01<<7)
#define RTS5228_DGLCH_TIME_MASK			(0x03<<5)
#define RTS5228_DGLCH_TIME_50			(0x00<<5)
#define RTS5228_DGLCH_TIME_75			(0x01<<5)
#define RTS5228_DGLCH_TIME_100			(0x02<<5)
#define RTS5228_DGLCH_TIME_125			(0x03<<5)
#define RTS5228_REG_REXT_TUNE_MASK		(0x1F<<0)

#define RTS5228_REG_PME_FORCE_CTL		0xFF78
#define FORCE_PM_CONTROL		0x20
#define FORCE_PM_VALUE			0x10


/* Single LUN, support SD */
#define DEFAULT_SINGLE		0
#define SD_LUN				1


/* For Change_FPGA_SSCClock Function */
#define MULTIPLY_BY_1    0x00
#define MULTIPLY_BY_2    0x01
#define MULTIPLY_BY_3    0x02
#define MULTIPLY_BY_4    0x03
#define MULTIPLY_BY_5    0x04
#define MULTIPLY_BY_6    0x05
#define MULTIPLY_BY_7    0x06
#define MULTIPLY_BY_8    0x07
#define MULTIPLY_BY_9    0x08
#define MULTIPLY_BY_10   0x09

#define DIVIDE_BY_2      0x01
#define DIVIDE_BY_3      0x02
#define DIVIDE_BY_4      0x03
#define DIVIDE_BY_5      0x04
#define DIVIDE_BY_6      0x05
#define DIVIDE_BY_7      0x06
#define DIVIDE_BY_8      0x07
#define DIVIDE_BY_9      0x08
#define DIVIDE_BY_10     0x09

int rts5228_pci_switch_clock(struct rtsx_pcr *pcr, unsigned int card_clock,
		u8 ssc_depth, bool initial_mode, bool double_clk, bool vpclk);

#endif /* RTS5228_H */
