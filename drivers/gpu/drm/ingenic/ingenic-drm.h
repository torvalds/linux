/* SPDX-License-Identifier: GPL-2.0 */
//
// Ingenic JZ47xx KMS driver - Register definitions and private API
//
// Copyright (C) 2020, Paul Cercueil <paul@crapouillou.net>

#ifndef DRIVERS_GPU_DRM_INGENIC_INGENIC_DRM_H
#define DRIVERS_GPU_DRM_INGENIC_INGENIC_DRM_H

#include <linux/bitops.h>
#include <linux/types.h>

#define JZ_REG_LCD_CFG				0x00
#define JZ_REG_LCD_VSYNC			0x04
#define JZ_REG_LCD_HSYNC			0x08
#define JZ_REG_LCD_VAT				0x0C
#define JZ_REG_LCD_DAH				0x10
#define JZ_REG_LCD_DAV				0x14
#define JZ_REG_LCD_PS				0x18
#define JZ_REG_LCD_CLS				0x1C
#define JZ_REG_LCD_SPL				0x20
#define JZ_REG_LCD_REV				0x24
#define JZ_REG_LCD_CTRL				0x30
#define JZ_REG_LCD_STATE			0x34
#define JZ_REG_LCD_IID				0x38
#define JZ_REG_LCD_DA0				0x40
#define JZ_REG_LCD_SA0				0x44
#define JZ_REG_LCD_FID0				0x48
#define JZ_REG_LCD_CMD0				0x4C
#define JZ_REG_LCD_DA1				0x50
#define JZ_REG_LCD_SA1				0x54
#define JZ_REG_LCD_FID1				0x58
#define JZ_REG_LCD_CMD1				0x5C
#define JZ_REG_LCD_RGBC				0x90
#define JZ_REG_LCD_OSDC				0x100
#define JZ_REG_LCD_OSDCTRL			0x104
#define JZ_REG_LCD_OSDS				0x108
#define JZ_REG_LCD_BGC				0x10c
#define JZ_REG_LCD_KEY0				0x110
#define JZ_REG_LCD_KEY1				0x114
#define JZ_REG_LCD_ALPHA			0x118
#define JZ_REG_LCD_IPUR				0x11c
#define JZ_REG_LCD_XYP0				0x120
#define JZ_REG_LCD_XYP1				0x124
#define JZ_REG_LCD_SIZE0			0x128
#define JZ_REG_LCD_SIZE1			0x12c
#define JZ_REG_LCD_PCFG				0x2c0

#define JZ_LCD_CFG_SLCD				BIT(31)
#define JZ_LCD_CFG_DESCRIPTOR_8			BIT(28)
#define JZ_LCD_CFG_RECOVER_FIFO_UNDERRUN	BIT(25)
#define JZ_LCD_CFG_PS_DISABLE			BIT(23)
#define JZ_LCD_CFG_CLS_DISABLE			BIT(22)
#define JZ_LCD_CFG_SPL_DISABLE			BIT(21)
#define JZ_LCD_CFG_REV_DISABLE			BIT(20)
#define JZ_LCD_CFG_HSYNCM			BIT(19)
#define JZ_LCD_CFG_PCLKM			BIT(18)
#define JZ_LCD_CFG_INV				BIT(17)
#define JZ_LCD_CFG_SYNC_DIR			BIT(16)
#define JZ_LCD_CFG_PS_POLARITY			BIT(15)
#define JZ_LCD_CFG_CLS_POLARITY			BIT(14)
#define JZ_LCD_CFG_SPL_POLARITY			BIT(13)
#define JZ_LCD_CFG_REV_POLARITY			BIT(12)
#define JZ_LCD_CFG_HSYNC_ACTIVE_LOW		BIT(11)
#define JZ_LCD_CFG_PCLK_FALLING_EDGE		BIT(10)
#define JZ_LCD_CFG_DE_ACTIVE_LOW		BIT(9)
#define JZ_LCD_CFG_VSYNC_ACTIVE_LOW		BIT(8)
#define JZ_LCD_CFG_18_BIT			BIT(7)
#define JZ_LCD_CFG_24_BIT			BIT(6)
#define JZ_LCD_CFG_PDW				(BIT(5) | BIT(4))

#define JZ_LCD_CFG_MODE_GENERIC_16BIT		0
#define JZ_LCD_CFG_MODE_GENERIC_18BIT		BIT(7)
#define JZ_LCD_CFG_MODE_GENERIC_24BIT		BIT(6)

#define JZ_LCD_CFG_MODE_SPECIAL_TFT_1		1
#define JZ_LCD_CFG_MODE_SPECIAL_TFT_2		2
#define JZ_LCD_CFG_MODE_SPECIAL_TFT_3		3

#define JZ_LCD_CFG_MODE_TV_OUT_P		4
#define JZ_LCD_CFG_MODE_TV_OUT_I		6

#define JZ_LCD_CFG_MODE_SINGLE_COLOR_STN	8
#define JZ_LCD_CFG_MODE_SINGLE_MONOCHROME_STN	9
#define JZ_LCD_CFG_MODE_DUAL_COLOR_STN		10
#define JZ_LCD_CFG_MODE_DUAL_MONOCHROME_STN	11

#define JZ_LCD_CFG_MODE_8BIT_SERIAL		12
#define JZ_LCD_CFG_MODE_LCM			13

#define JZ_LCD_VSYNC_VPS_OFFSET			16
#define JZ_LCD_VSYNC_VPE_OFFSET			0

#define JZ_LCD_HSYNC_HPS_OFFSET			16
#define JZ_LCD_HSYNC_HPE_OFFSET			0

#define JZ_LCD_VAT_HT_OFFSET			16
#define JZ_LCD_VAT_VT_OFFSET			0

#define JZ_LCD_DAH_HDS_OFFSET			16
#define JZ_LCD_DAH_HDE_OFFSET			0

#define JZ_LCD_DAV_VDS_OFFSET			16
#define JZ_LCD_DAV_VDE_OFFSET			0

#define JZ_LCD_CTRL_BURST_4			(0x0 << 28)
#define JZ_LCD_CTRL_BURST_8			(0x1 << 28)
#define JZ_LCD_CTRL_BURST_16			(0x2 << 28)
#define JZ_LCD_CTRL_BURST_32			(0x3 << 28)
#define JZ_LCD_CTRL_BURST_64			(0x4 << 28)
#define JZ_LCD_CTRL_BURST_MASK			(0x7 << 28)
#define JZ_LCD_CTRL_RGB555			BIT(27)
#define JZ_LCD_CTRL_OFUP			BIT(26)
#define JZ_LCD_CTRL_FRC_GRAYSCALE_16		(0x0 << 24)
#define JZ_LCD_CTRL_FRC_GRAYSCALE_4		(0x1 << 24)
#define JZ_LCD_CTRL_FRC_GRAYSCALE_2		(0x2 << 24)
#define JZ_LCD_CTRL_PDD_MASK			(0xff << 16)
#define JZ_LCD_CTRL_EOF_IRQ			BIT(13)
#define JZ_LCD_CTRL_SOF_IRQ			BIT(12)
#define JZ_LCD_CTRL_OFU_IRQ			BIT(11)
#define JZ_LCD_CTRL_IFU0_IRQ			BIT(10)
#define JZ_LCD_CTRL_IFU1_IRQ			BIT(9)
#define JZ_LCD_CTRL_DD_IRQ			BIT(8)
#define JZ_LCD_CTRL_QDD_IRQ			BIT(7)
#define JZ_LCD_CTRL_REVERSE_ENDIAN		BIT(6)
#define JZ_LCD_CTRL_LSB_FISRT			BIT(5)
#define JZ_LCD_CTRL_DISABLE			BIT(4)
#define JZ_LCD_CTRL_ENABLE			BIT(3)
#define JZ_LCD_CTRL_BPP_1			0x0
#define JZ_LCD_CTRL_BPP_2			0x1
#define JZ_LCD_CTRL_BPP_4			0x2
#define JZ_LCD_CTRL_BPP_8			0x3
#define JZ_LCD_CTRL_BPP_15_16			0x4
#define JZ_LCD_CTRL_BPP_18_24			0x5
#define JZ_LCD_CTRL_BPP_24_COMP			0x6
#define JZ_LCD_CTRL_BPP_30			0x7
#define JZ_LCD_CTRL_BPP_MASK			(JZ_LCD_CTRL_RGB555 | 0x7)

#define JZ_LCD_CMD_SOF_IRQ			BIT(31)
#define JZ_LCD_CMD_EOF_IRQ			BIT(30)
#define JZ_LCD_CMD_ENABLE_PAL			BIT(28)
#define JZ_LCD_CMD_FRM_ENABLE			BIT(26)

#define JZ_LCD_SYNC_MASK			0x3ff

#define JZ_LCD_STATE_EOF_IRQ			BIT(5)
#define JZ_LCD_STATE_SOF_IRQ			BIT(4)
#define JZ_LCD_STATE_DISABLED			BIT(0)

#define JZ_LCD_RGBC_ODD_RGB			(0x0 << 4)
#define JZ_LCD_RGBC_ODD_RBG			(0x1 << 4)
#define JZ_LCD_RGBC_ODD_GRB			(0x2 << 4)
#define JZ_LCD_RGBC_ODD_GBR			(0x3 << 4)
#define JZ_LCD_RGBC_ODD_BRG			(0x4 << 4)
#define JZ_LCD_RGBC_ODD_BGR			(0x5 << 4)
#define JZ_LCD_RGBC_EVEN_RGB			(0x0 << 0)
#define JZ_LCD_RGBC_EVEN_RBG			(0x1 << 0)
#define JZ_LCD_RGBC_EVEN_GRB			(0x2 << 0)
#define JZ_LCD_RGBC_EVEN_GBR			(0x3 << 0)
#define JZ_LCD_RGBC_EVEN_BRG			(0x4 << 0)
#define JZ_LCD_RGBC_EVEN_BGR			(0x5 << 0)

#define JZ_LCD_OSDC_OSDEN			BIT(0)
#define JZ_LCD_OSDC_ALPHAEN			BIT(2)
#define JZ_LCD_OSDC_F0EN			BIT(3)
#define JZ_LCD_OSDC_F1EN			BIT(4)

#define JZ_LCD_OSDCTRL_IPU			BIT(15)
#define JZ_LCD_OSDCTRL_RGB555			BIT(4)
#define JZ_LCD_OSDCTRL_CHANGE			BIT(3)
#define JZ_LCD_OSDCTRL_BPP_15_16		0x4
#define JZ_LCD_OSDCTRL_BPP_18_24		0x5
#define JZ_LCD_OSDCTRL_BPP_24_COMP		0x6
#define JZ_LCD_OSDCTRL_BPP_30			0x7
#define JZ_LCD_OSDCTRL_BPP_MASK			(JZ_LCD_OSDCTRL_RGB555 | 0x7)

#define JZ_LCD_OSDS_READY			BIT(0)

#define JZ_LCD_IPUR_IPUREN			BIT(31)
#define JZ_LCD_IPUR_IPUR_LSB			0

#define JZ_LCD_XYP01_XPOS_LSB			0
#define JZ_LCD_XYP01_YPOS_LSB			16

#define JZ_LCD_SIZE01_WIDTH_LSB			0
#define JZ_LCD_SIZE01_HEIGHT_LSB		16

#define JZ_LCD_DESSIZE_ALPHA_OFFSET		24
#define JZ_LCD_DESSIZE_HEIGHT_MASK		GENMASK(23, 12)
#define JZ_LCD_DESSIZE_WIDTH_MASK		GENMASK(11, 0)

#define JZ_LCD_CPOS_BPP_15_16			(4 << 27)
#define JZ_LCD_CPOS_BPP_18_24			(5 << 27)
#define JZ_LCD_CPOS_BPP_30			(7 << 27)
#define JZ_LCD_CPOS_RGB555			BIT(30)
#define JZ_LCD_CPOS_PREMULTIPLY_LCD		BIT(26)
#define JZ_LCD_CPOS_COEFFICIENT_OFFSET		24
#define JZ_LCD_CPOS_COEFFICIENT_0		0
#define JZ_LCD_CPOS_COEFFICIENT_1		1
#define JZ_LCD_CPOS_COEFFICIENT_ALPHA1		2
#define JZ_LCD_CPOS_COEFFICIENT_1_ALPHA1	3

#define JZ_LCD_RGBC_RGB_PADDING			BIT(15)
#define JZ_LCD_RGBC_RGB_PADDING_FIRST		BIT(14)
#define JZ_LCD_RGBC_422				BIT(8)
#define JZ_LCD_RGBC_RGB_FORMAT_ENABLE		BIT(7)

#define JZ_LCD_PCFG_PRI_MODE			BIT(31)
#define JZ_LCD_PCFG_HP_BST_4			(0 << 28)
#define JZ_LCD_PCFG_HP_BST_8			(1 << 28)
#define JZ_LCD_PCFG_HP_BST_16			(2 << 28)
#define JZ_LCD_PCFG_HP_BST_32			(3 << 28)
#define JZ_LCD_PCFG_HP_BST_64			(4 << 28)
#define JZ_LCD_PCFG_HP_BST_16_CONT		(5 << 28)
#define JZ_LCD_PCFG_HP_BST_DISABLE		(7 << 28)
#define JZ_LCD_PCFG_THRESHOLD2_OFFSET		18
#define JZ_LCD_PCFG_THRESHOLD1_OFFSET		9
#define JZ_LCD_PCFG_THRESHOLD0_OFFSET		0

struct device;
struct drm_plane;
struct drm_plane_state;
struct platform_driver;

void ingenic_drm_plane_config(struct device *dev,
			      struct drm_plane *plane, u32 fourcc);
void ingenic_drm_plane_disable(struct device *dev, struct drm_plane *plane);
bool ingenic_drm_map_noncoherent(const struct device *dev);

extern struct platform_driver *ingenic_ipu_driver_ptr;

#endif /* DRIVERS_GPU_DRM_INGENIC_INGENIC_DRM_H */
