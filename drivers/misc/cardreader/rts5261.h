/* SPDX-License-Identifier: GPL-2.0-only */
/* Driver for Realtek PCI-Express card reader
 *
 * Copyright(c) 2018-2019 Realtek Semiconductor Corp. All rights reserved.
 *
 * Author:
 *   Rui FENG <rui_feng@realsil.com.cn>
 *   Wei WANG <wei_wang@realsil.com.cn>
 */
#ifndef RTS5261_H
#define RTS5261_H

/*New add*/
#define rts5261_vendor_setting_valid(reg)	((reg) & 0x010000)
#define rts5261_reg_to_aspm(reg) \
	(((~(reg) >> 28) & 0x02) | (((reg) >> 28) & 0x01))
#define rts5261_reg_check_reverse_socket(reg)	((reg) & 0x04)
#define rts5261_reg_to_sd30_drive_sel_1v8(reg)	(((reg) >> 22) & 0x03)
#define rts5261_reg_to_sd30_drive_sel_3v3(reg)	(((reg) >> 16) & 0x03)
#define rts5261_reg_to_rtd3(reg)		((reg) & 0x08)
#define rts5261_reg_check_mmc_support(reg)	((reg) & 0x10)

#define RTS5261_AUTOLOAD_CFG0		0xFF7B
#define RTS5261_AUTOLOAD_CFG1		0xFF7C
#define RTS5261_AUTOLOAD_CFG2		0xFF7D
#define RTS5261_AUTOLOAD_CFG3		0xFF7E
#define RTS5261_AUTOLOAD_CFG4		0xFF7F
#define RTS5261_FORCE_PRSNT_LOW		(1 << 6)
#define RTS5261_AUX_CLK_16M_EN		(1 << 5)

#define RTS5261_REG_VREF		0xFE97
#define RTS5261_PWD_SUSPND_EN		(1 << 4)

#define RTS5261_PAD_H3L1		0xFF79
#define PAD_GPIO_H3L1			(1 << 3)

/* SSC_CTL2 0xFC12 */
#define RTS5261_SSC_DEPTH_MASK		0x07
#define RTS5261_SSC_DEPTH_DISALBE	0x00
#define RTS5261_SSC_DEPTH_8M		0x01
#define RTS5261_SSC_DEPTH_4M		0x02
#define RTS5261_SSC_DEPTH_2M		0x03
#define RTS5261_SSC_DEPTH_1M		0x04
#define RTS5261_SSC_DEPTH_512K		0x05
#define RTS5261_SSC_DEPTH_256K		0x06
#define RTS5261_SSC_DEPTH_128K		0x07

/* efuse control register*/
#define RTS5261_EFUSE_CTL		0xFC30
#define RTS5261_EFUSE_ENABLE		0x80
/* EFUSE_MODE: 0=READ 1=PROGRAM */
#define RTS5261_EFUSE_MODE_MASK		0x40
#define RTS5261_EFUSE_PROGRAM		0x40

#define RTS5261_EFUSE_ADDR		0xFC31
#define	RTS5261_EFUSE_ADDR_MASK		0x3F

#define RTS5261_EFUSE_WRITE_DATA	0xFC32
#define RTS5261_EFUSE_READ_DATA		0xFC34

/* DMACTL 0xFE2C */
#define RTS5261_DMA_PACK_SIZE_MASK	0xF0

/* FW status register */
#define RTS5261_FW_STATUS		0xFF56
#define RTS5261_EXPRESS_LINK_FAIL_MASK	(0x01<<7)

/* FW control register */
#define RTS5261_FW_CTL			0xFF5F
#define RTS5261_INFORM_RTD3_COLD	(0x01<<5)

#define RTS5261_REG_FPDCTL		0xFF60

#define RTS5261_REG_LDO12_CFG		0xFF6E
#define RTS5261_LDO12_VO_TUNE_MASK	(0x07<<1)
#define RTS5261_LDO12_115		(0x03<<1)
#define RTS5261_LDO12_120		(0x04<<1)
#define RTS5261_LDO12_125		(0x05<<1)
#define RTS5261_LDO12_130		(0x06<<1)
#define RTS5261_LDO12_135		(0x07<<1)

/* LDO control register */
#define RTS5261_CARD_PWR_CTL		0xFD50
#define RTS5261_SD_CLK_ISO		(0x01<<7)
#define RTS5261_PAD_SD_DAT_FW_CTRL	(0x01<<6)
#define RTS5261_PUPDC			(0x01<<5)
#define RTS5261_SD_CMD_ISO		(0x01<<4)
#define RTS5261_SD_DAT_ISO_MASK		(0x0F<<0)

#define RTS5261_LDO1233318_POW_CTL	0xFF70
#define RTS5261_LDO3318_POWERON		(0x01<<3)
#define RTS5261_LDO3_POWERON		(0x01<<2)
#define RTS5261_LDO2_POWERON		(0x01<<1)
#define RTS5261_LDO1_POWERON		(0x01<<0)
#define RTS5261_LDO_POWERON_MASK	(0x0F<<0)

#define RTS5261_DV3318_CFG		0xFF71
#define RTS5261_DV3318_TUNE_MASK	(0x07<<4)
#define RTS5261_DV3318_18		(0x02<<4)
#define RTS5261_DV3318_19		(0x04<<4)
#define RTS5261_DV3318_33		(0x07<<4)

/* CRD6603-433 190319 request changed */
#define RTS5261_LDO1_OCP_THD_740	(0x00<<5)
#define RTS5261_LDO1_OCP_THD_800	(0x01<<5)
#define RTS5261_LDO1_OCP_THD_860	(0x02<<5)
#define RTS5261_LDO1_OCP_THD_920	(0x03<<5)
#define RTS5261_LDO1_OCP_THD_980	(0x04<<5)
#define RTS5261_LDO1_OCP_THD_1040	(0x05<<5)
#define RTS5261_LDO1_OCP_THD_1100	(0x06<<5)
#define RTS5261_LDO1_OCP_THD_1160	(0x07<<5)

#define RTS5261_LDO1_LMT_THD_450	(0x00<<2)
#define RTS5261_LDO1_LMT_THD_1000	(0x01<<2)
#define RTS5261_LDO1_LMT_THD_1500	(0x02<<2)
#define RTS5261_LDO1_LMT_THD_2000	(0x03<<2)

#define RTS5261_LDO1_CFG1		0xFF73
#define RTS5261_LDO1_TUNE_MASK		(0x07<<1)
#define RTS5261_LDO1_18			(0x05<<1)
#define RTS5261_LDO1_33			(0x07<<1)
#define RTS5261_LDO1_PWD_MASK		(0x01<<0)

#define RTS5261_LDO2_CFG0		0xFF74
#define RTS5261_LDO2_OCP_THD_MASK	(0x07<<5)
#define RTS5261_LDO2_OCP_EN		(0x01<<4)
#define RTS5261_LDO2_OCP_LMT_THD_MASK	(0x03<<2)
#define RTS5261_LDO2_OCP_LMT_EN		(0x01<<1)

#define RTS5261_LDO2_OCP_THD_620	(0x00<<5)
#define RTS5261_LDO2_OCP_THD_650	(0x01<<5)
#define RTS5261_LDO2_OCP_THD_680	(0x02<<5)
#define RTS5261_LDO2_OCP_THD_720	(0x03<<5)
#define RTS5261_LDO2_OCP_THD_750	(0x04<<5)
#define RTS5261_LDO2_OCP_THD_780	(0x05<<5)
#define RTS5261_LDO2_OCP_THD_810	(0x06<<5)
#define RTS5261_LDO2_OCP_THD_840	(0x07<<5)

#define RTS5261_LDO2_CFG1		0xFF75
#define RTS5261_LDO2_TUNE_MASK		(0x07<<1)
#define RTS5261_LDO2_18			(0x05<<1)
#define RTS5261_LDO2_33			(0x07<<1)
#define RTS5261_LDO2_PWD_MASK		(0x01<<0)

#define RTS5261_LDO3_CFG0		0xFF76
#define RTS5261_LDO3_OCP_THD_MASK	(0x07<<5)
#define RTS5261_LDO3_OCP_EN		(0x01<<4)
#define RTS5261_LDO3_OCP_LMT_THD_MASK	(0x03<<2)
#define RTS5261_LDO3_OCP_LMT_EN		(0x01<<1)

#define RTS5261_LDO3_OCP_THD_620	(0x00<<5)
#define RTS5261_LDO3_OCP_THD_650	(0x01<<5)
#define RTS5261_LDO3_OCP_THD_680	(0x02<<5)
#define RTS5261_LDO3_OCP_THD_720	(0x03<<5)
#define RTS5261_LDO3_OCP_THD_750	(0x04<<5)
#define RTS5261_LDO3_OCP_THD_780	(0x05<<5)
#define RTS5261_LDO3_OCP_THD_810	(0x06<<5)
#define RTS5261_LDO3_OCP_THD_840	(0x07<<5)

#define RTS5261_LDO3_CFG1		0xFF77
#define RTS5261_LDO3_TUNE_MASK		(0x07<<1)
#define RTS5261_LDO3_18			(0x05<<1)
#define RTS5261_LDO3_33			(0x07<<1)
#define RTS5261_LDO3_PWD_MASK		(0x01<<0)

#define RTS5261_REG_PME_FORCE_CTL	0xFF78
#define FORCE_PM_CONTROL		0x20
#define FORCE_PM_VALUE			0x10
#define REG_EFUSE_BYPASS		0x08
#define REG_EFUSE_POR			0x04
#define REG_EFUSE_POWER_MASK		0x03
#define REG_EFUSE_POWERON		0x03
#define REG_EFUSE_POWEROFF		0x00


/* Single LUN, support SD/SD EXPRESS */
#define DEFAULT_SINGLE		0
#define SD_LUN			1
#define SD_EXPRESS_LUN		2

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

int rts5261_pci_switch_clock(struct rtsx_pcr *pcr, unsigned int card_clock,
		u8 ssc_depth, bool initial_mode, bool double_clk, bool vpclk);

#endif /* RTS5261_H */
