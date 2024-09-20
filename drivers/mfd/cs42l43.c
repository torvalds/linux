// SPDX-License-Identifier: GPL-2.0
/*
 * CS42L43 core driver
 *
 * Copyright (C) 2022-2023 Cirrus Logic, Inc. and
 *                         Cirrus Logic International Semiconductor Ltd.
 */

#include <linux/array_size.h>
#include <linux/bitops.h>
#include <linux/build_bug.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/jiffies.h>
#include <linux/mfd/core.h>
#include <linux/mfd/cs42l43.h>
#include <linux/mfd/cs42l43-regs.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/types.h>

#include "cs42l43.h"

#define CS42L43_RESET_DELAY_MS			20

#define CS42L43_SDW_ATTACH_TIMEOUT_MS		500
#define CS42L43_SDW_DETACH_TIMEOUT_MS		100

#define CS42L43_MCU_BOOT_STAGE1			1
#define CS42L43_MCU_BOOT_STAGE2			2
#define CS42L43_MCU_BOOT_STAGE3			3
#define CS42L43_MCU_BOOT_STAGE4			4
#define CS42L43_MCU_POLL_US			5000
#define CS42L43_MCU_CMD_TIMEOUT_US		20000
#define CS42L43_MCU_UPDATE_FORMAT		3
#define CS42L43_MCU_UPDATE_OFFSET		0x100000
#define CS42L43_MCU_UPDATE_TIMEOUT_US		500000
#define CS42L43_MCU_UPDATE_RETRIES		5

#define CS42L43_MCU_ROM_REV			0x2001
#define CS42L43_MCU_ROM_BIOS_REV		0x0000

#define CS42L43_MCU_SUPPORTED_REV		0x2105
#define CS42L43_MCU_SHADOW_REGS_REQUIRED_REV	0x2200
#define CS42L43_MCU_SUPPORTED_BIOS_REV		0x0001

#define CS42L43_VDDP_DELAY_US			50
#define CS42L43_VDDD_DELAY_US			1000

#define CS42L43_AUTOSUSPEND_TIME_MS		250

struct cs42l43_patch_header {
	__le16 version;
	__le16 size;
	__u8 reserved;
	__u8 secure;
	__le16 bss_size;
	__le32 apply_addr;
	__le32 checksum;
	__le32 sha;
	__le16 swrev;
	__le16 patchid;
	__le16 ipxid;
	__le16 romver;
	__le32 load_addr;
} __packed;

static const struct reg_sequence cs42l43_reva_patch[] = {
	{ 0x4000,					0x00000055 },
	{ 0x4000,					0x000000AA },
	{ 0x10084,					0x00000000 },
	{ 0x1741C,					0x00CD2000 },
	{ 0x1718C,					0x00000003 },
	{ 0x4000,					0x00000000 },
	{ CS42L43_CCM_BLK_CLK_CONTROL,			0x00000002 },
	{ CS42L43_HPPATHVOL,				0x011B011B },
	{ CS42L43_OSC_DIV_SEL,				0x00000001 },
	{ CS42L43_DACCNFG2,				0x00000005 },
	{ CS42L43_MIC_DETECT_CONTROL_ANDROID,		0x80790079 },
	{ CS42L43_RELID,				0x0000000F },
};

const struct reg_default cs42l43_reg_default[CS42L43_N_DEFAULTS] = {
	{ CS42L43_DRV_CTRL1,				0x000186C0 },
	{ CS42L43_DRV_CTRL3,				0x286DB018 },
	{ CS42L43_DRV_CTRL4,				0x000006D8 },
	{ CS42L43_DRV_CTRL_5,				0x136C00C0 },
	{ CS42L43_GPIO_CTRL1,				0x00000707 },
	{ CS42L43_GPIO_CTRL2,				0x00000000 },
	{ CS42L43_GPIO_FN_SEL,				0x00000004 },
	{ CS42L43_MCLK_SRC_SEL,				0x00000000 },
	{ CS42L43_SAMPLE_RATE1,				0x00000003 },
	{ CS42L43_SAMPLE_RATE2,				0x00000003 },
	{ CS42L43_SAMPLE_RATE3,				0x00000003 },
	{ CS42L43_SAMPLE_RATE4,				0x00000003 },
	{ CS42L43_PLL_CONTROL,				0x00000000 },
	{ CS42L43_FS_SELECT1,				0x00000000 },
	{ CS42L43_FS_SELECT2,				0x00000000 },
	{ CS42L43_FS_SELECT3,				0x00000000 },
	{ CS42L43_FS_SELECT4,				0x00000000 },
	{ CS42L43_PDM_CONTROL,				0x00000000 },
	{ CS42L43_ASP_CLK_CONFIG1,			0x00010001 },
	{ CS42L43_ASP_CLK_CONFIG2,			0x00000000 },
	{ CS42L43_OSC_DIV_SEL,				0x00000001 },
	{ CS42L43_ADC_B_CTRL1,				0x00000000 },
	{ CS42L43_ADC_B_CTRL2,				0x00000000 },
	{ CS42L43_DECIM_HPF_WNF_CTRL1,			0x00000001 },
	{ CS42L43_DECIM_HPF_WNF_CTRL2,			0x00000001 },
	{ CS42L43_DECIM_HPF_WNF_CTRL3,			0x00000001 },
	{ CS42L43_DECIM_HPF_WNF_CTRL4,			0x00000001 },
	{ CS42L43_DMIC_PDM_CTRL,			0x00000000 },
	{ CS42L43_DECIM_VOL_CTRL_CH1_CH2,		0x20122012 },
	{ CS42L43_DECIM_VOL_CTRL_CH3_CH4,		0x20122012 },
	{ CS42L43_INTP_VOLUME_CTRL1,			0x00000180 },
	{ CS42L43_INTP_VOLUME_CTRL2,			0x00000180 },
	{ CS42L43_AMP1_2_VOL_RAMP,			0x00000022 },
	{ CS42L43_ASP_CTRL,				0x00000004 },
	{ CS42L43_ASP_FSYNC_CTRL1,			0x000000FA },
	{ CS42L43_ASP_FSYNC_CTRL2,			0x00000001 },
	{ CS42L43_ASP_FSYNC_CTRL3,			0x00000000 },
	{ CS42L43_ASP_FSYNC_CTRL4,			0x000001F4 },
	{ CS42L43_ASP_DATA_CTRL,			0x0000003A },
	{ CS42L43_ASP_RX_EN,				0x00000000 },
	{ CS42L43_ASP_TX_EN,				0x00000000 },
	{ CS42L43_ASP_RX_CH1_CTRL,			0x00170001 },
	{ CS42L43_ASP_RX_CH2_CTRL,			0x00170031 },
	{ CS42L43_ASP_RX_CH3_CTRL,			0x00170061 },
	{ CS42L43_ASP_RX_CH4_CTRL,			0x00170091 },
	{ CS42L43_ASP_RX_CH5_CTRL,			0x001700C1 },
	{ CS42L43_ASP_RX_CH6_CTRL,			0x001700F1 },
	{ CS42L43_ASP_TX_CH1_CTRL,			0x00170001 },
	{ CS42L43_ASP_TX_CH2_CTRL,			0x00170031 },
	{ CS42L43_ASP_TX_CH3_CTRL,			0x00170061 },
	{ CS42L43_ASP_TX_CH4_CTRL,			0x00170091 },
	{ CS42L43_ASP_TX_CH5_CTRL,			0x001700C1 },
	{ CS42L43_ASP_TX_CH6_CTRL,			0x001700F1 },
	{ CS42L43_ASPTX1_INPUT,				0x00000000 },
	{ CS42L43_ASPTX2_INPUT,				0x00000000 },
	{ CS42L43_ASPTX3_INPUT,				0x00000000 },
	{ CS42L43_ASPTX4_INPUT,				0x00000000 },
	{ CS42L43_ASPTX5_INPUT,				0x00000000 },
	{ CS42L43_ASPTX6_INPUT,				0x00000000 },
	{ CS42L43_SWIRE_DP1_CH1_INPUT,			0x00000000 },
	{ CS42L43_SWIRE_DP1_CH2_INPUT,			0x00000000 },
	{ CS42L43_SWIRE_DP1_CH3_INPUT,			0x00000000 },
	{ CS42L43_SWIRE_DP1_CH4_INPUT,			0x00000000 },
	{ CS42L43_SWIRE_DP2_CH1_INPUT,			0x00000000 },
	{ CS42L43_SWIRE_DP2_CH2_INPUT,			0x00000000 },
	{ CS42L43_SWIRE_DP3_CH1_INPUT,			0x00000000 },
	{ CS42L43_SWIRE_DP3_CH2_INPUT,			0x00000000 },
	{ CS42L43_SWIRE_DP4_CH1_INPUT,			0x00000000 },
	{ CS42L43_SWIRE_DP4_CH2_INPUT,			0x00000000 },
	{ CS42L43_ASRC_INT1_INPUT1,			0x00000000 },
	{ CS42L43_ASRC_INT2_INPUT1,			0x00000000 },
	{ CS42L43_ASRC_INT3_INPUT1,			0x00000000 },
	{ CS42L43_ASRC_INT4_INPUT1,			0x00000000 },
	{ CS42L43_ASRC_DEC1_INPUT1,			0x00000000 },
	{ CS42L43_ASRC_DEC2_INPUT1,			0x00000000 },
	{ CS42L43_ASRC_DEC3_INPUT1,			0x00000000 },
	{ CS42L43_ASRC_DEC4_INPUT1,			0x00000000 },
	{ CS42L43_ISRC1INT1_INPUT1,			0x00000000 },
	{ CS42L43_ISRC1INT2_INPUT1,			0x00000000 },
	{ CS42L43_ISRC1DEC1_INPUT1,			0x00000000 },
	{ CS42L43_ISRC1DEC2_INPUT1,			0x00000000 },
	{ CS42L43_ISRC2INT1_INPUT1,			0x00000000 },
	{ CS42L43_ISRC2INT2_INPUT1,			0x00000000 },
	{ CS42L43_ISRC2DEC1_INPUT1,			0x00000000 },
	{ CS42L43_ISRC2DEC2_INPUT1,			0x00000000 },
	{ CS42L43_EQ1MIX_INPUT1,			0x00800000 },
	{ CS42L43_EQ1MIX_INPUT2,			0x00800000 },
	{ CS42L43_EQ1MIX_INPUT3,			0x00800000 },
	{ CS42L43_EQ1MIX_INPUT4,			0x00800000 },
	{ CS42L43_EQ2MIX_INPUT1,			0x00800000 },
	{ CS42L43_EQ2MIX_INPUT2,			0x00800000 },
	{ CS42L43_EQ2MIX_INPUT3,			0x00800000 },
	{ CS42L43_EQ2MIX_INPUT4,			0x00800000 },
	{ CS42L43_SPDIF1_INPUT1,			0x00000000 },
	{ CS42L43_SPDIF2_INPUT1,			0x00000000 },
	{ CS42L43_AMP1MIX_INPUT1,			0x00800000 },
	{ CS42L43_AMP1MIX_INPUT2,			0x00800000 },
	{ CS42L43_AMP1MIX_INPUT3,			0x00800000 },
	{ CS42L43_AMP1MIX_INPUT4,			0x00800000 },
	{ CS42L43_AMP2MIX_INPUT1,			0x00800000 },
	{ CS42L43_AMP2MIX_INPUT2,			0x00800000 },
	{ CS42L43_AMP2MIX_INPUT3,			0x00800000 },
	{ CS42L43_AMP2MIX_INPUT4,			0x00800000 },
	{ CS42L43_AMP3MIX_INPUT1,			0x00800000 },
	{ CS42L43_AMP3MIX_INPUT2,			0x00800000 },
	{ CS42L43_AMP3MIX_INPUT3,			0x00800000 },
	{ CS42L43_AMP3MIX_INPUT4,			0x00800000 },
	{ CS42L43_AMP4MIX_INPUT1,			0x00800000 },
	{ CS42L43_AMP4MIX_INPUT2,			0x00800000 },
	{ CS42L43_AMP4MIX_INPUT3,			0x00800000 },
	{ CS42L43_AMP4MIX_INPUT4,			0x00800000 },
	{ CS42L43_ASRC_INT_ENABLES,			0x00000100 },
	{ CS42L43_ASRC_DEC_ENABLES,			0x00000100 },
	{ CS42L43_PDNCNTL,				0x00000000 },
	{ CS42L43_RINGSENSE_DEB_CTRL,			0x0000001B },
	{ CS42L43_TIPSENSE_DEB_CTRL,			0x0000001B },
	{ CS42L43_HS2,					0x050106F3 },
	{ CS42L43_STEREO_MIC_CTRL,			0x00000000 },
	{ CS42L43_STEREO_MIC_CLAMP_CTRL,		0x00000001 },
	{ CS42L43_BLOCK_EN2,				0x00000000 },
	{ CS42L43_BLOCK_EN3,				0x00000000 },
	{ CS42L43_BLOCK_EN4,				0x00000000 },
	{ CS42L43_BLOCK_EN5,				0x00000000 },
	{ CS42L43_BLOCK_EN6,				0x00000000 },
	{ CS42L43_BLOCK_EN7,				0x00000000 },
	{ CS42L43_BLOCK_EN8,				0x00000000 },
	{ CS42L43_BLOCK_EN9,				0x00000000 },
	{ CS42L43_BLOCK_EN10,				0x00000000 },
	{ CS42L43_BLOCK_EN11,				0x00000000 },
	{ CS42L43_TONE_CH1_CTRL,			0x00000000 },
	{ CS42L43_TONE_CH2_CTRL,			0x00000000 },
	{ CS42L43_MIC_DETECT_CONTROL_1,			0x00000003 },
	{ CS42L43_HS_BIAS_SENSE_AND_CLAMP_AUTOCONTROL,	0x02000003 },
	{ CS42L43_MIC_DETECT_CONTROL_ANDROID,		0x80790079 },
	{ CS42L43_ISRC1_CTRL,				0x00000000 },
	{ CS42L43_ISRC2_CTRL,				0x00000000 },
	{ CS42L43_CTRL_REG,				0x00000006 },
	{ CS42L43_FDIV_FRAC,				0x40000000 },
	{ CS42L43_CAL_RATIO,				0x00000080 },
	{ CS42L43_SPI_CLK_CONFIG1,			0x00000001 },
	{ CS42L43_SPI_CONFIG1,				0x00000000 },
	{ CS42L43_SPI_CONFIG2,				0x00000000 },
	{ CS42L43_SPI_CONFIG3,				0x00000001 },
	{ CS42L43_SPI_CONFIG4,				0x00000000 },
	{ CS42L43_TRAN_CONFIG3,				0x00000000 },
	{ CS42L43_TRAN_CONFIG4,				0x00000000 },
	{ CS42L43_TRAN_CONFIG5,				0x00000000 },
	{ CS42L43_TRAN_CONFIG6,				0x00000000 },
	{ CS42L43_TRAN_CONFIG7,				0x00000000 },
	{ CS42L43_TRAN_CONFIG8,				0x00000000 },
	{ CS42L43_DACCNFG1,				0x00000008 },
	{ CS42L43_DACCNFG2,				0x00000005 },
	{ CS42L43_HPPATHVOL,				0x011B011B },
	{ CS42L43_PGAVOL,				0x00003470 },
	{ CS42L43_LOADDETENA,				0x00000000 },
	{ CS42L43_CTRL,					0x00000037 },
	{ CS42L43_COEFF_DATA_IN0,			0x00000000 },
	{ CS42L43_COEFF_RD_WR0,				0x00000000 },
	{ CS42L43_START_EQZ0,				0x00000000 },
	{ CS42L43_MUTE_EQ_IN0,				0x00000000 },
	{ CS42L43_DECIM_MASK,				0x0000000F },
	{ CS42L43_EQ_MIX_MASK,				0x0000000F },
	{ CS42L43_ASP_MASK,				0x000000FF },
	{ CS42L43_PLL_MASK,				0x00000003 },
	{ CS42L43_SOFT_MASK,				0x0000FFFF },
	{ CS42L43_SWIRE_MASK,				0x00007FFF },
	{ CS42L43_MSM_MASK,				0x00000FFF },
	{ CS42L43_ACC_DET_MASK,				0x00000FFF },
	{ CS42L43_I2C_TGT_MASK,				0x00000003 },
	{ CS42L43_SPI_MSTR_MASK,			0x00000007 },
	{ CS42L43_SW_TO_SPI_BRIDGE_MASK,		0x00000001 },
	{ CS42L43_OTP_MASK,				0x00000007 },
	{ CS42L43_CLASS_D_AMP_MASK,			0x00003FFF },
	{ CS42L43_GPIO_INT_MASK,			0x0000003F },
	{ CS42L43_ASRC_MASK,				0x0000000F },
	{ CS42L43_HPOUT_MASK,				0x00000003 },
};
EXPORT_SYMBOL_NS_GPL(cs42l43_reg_default, MFD_CS42L43);

bool cs42l43_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS42L43_DEVID:
	case CS42L43_REVID:
	case CS42L43_RELID:
	case CS42L43_SFT_RESET:
	case CS42L43_DRV_CTRL1:
	case CS42L43_DRV_CTRL3:
	case CS42L43_DRV_CTRL4:
	case CS42L43_DRV_CTRL_5:
	case CS42L43_GPIO_CTRL1:
	case CS42L43_GPIO_CTRL2:
	case CS42L43_GPIO_STS:
	case CS42L43_GPIO_FN_SEL:
	case CS42L43_MCLK_SRC_SEL:
	case CS42L43_SAMPLE_RATE1 ... CS42L43_SAMPLE_RATE4:
	case CS42L43_PLL_CONTROL:
	case CS42L43_FS_SELECT1 ... CS42L43_FS_SELECT4:
	case CS42L43_PDM_CONTROL:
	case CS42L43_ASP_CLK_CONFIG1 ... CS42L43_ASP_CLK_CONFIG2:
	case CS42L43_OSC_DIV_SEL:
	case CS42L43_ADC_B_CTRL1 ...  CS42L43_ADC_B_CTRL2:
	case CS42L43_DECIM_HPF_WNF_CTRL1 ... CS42L43_DECIM_HPF_WNF_CTRL4:
	case CS42L43_DMIC_PDM_CTRL:
	case CS42L43_DECIM_VOL_CTRL_CH1_CH2 ... CS42L43_DECIM_VOL_CTRL_CH3_CH4:
	case CS42L43_INTP_VOLUME_CTRL1 ... CS42L43_INTP_VOLUME_CTRL2:
	case CS42L43_AMP1_2_VOL_RAMP:
	case CS42L43_ASP_CTRL:
	case CS42L43_ASP_FSYNC_CTRL1 ... CS42L43_ASP_FSYNC_CTRL4:
	case CS42L43_ASP_DATA_CTRL:
	case CS42L43_ASP_RX_EN ... CS42L43_ASP_TX_EN:
	case CS42L43_ASP_RX_CH1_CTRL ... CS42L43_ASP_RX_CH6_CTRL:
	case CS42L43_ASP_TX_CH1_CTRL ... CS42L43_ASP_TX_CH6_CTRL:
	case CS42L43_OTP_REVISION_ID:
	case CS42L43_ASPTX1_INPUT:
	case CS42L43_ASPTX2_INPUT:
	case CS42L43_ASPTX3_INPUT:
	case CS42L43_ASPTX4_INPUT:
	case CS42L43_ASPTX5_INPUT:
	case CS42L43_ASPTX6_INPUT:
	case CS42L43_SWIRE_DP1_CH1_INPUT:
	case CS42L43_SWIRE_DP1_CH2_INPUT:
	case CS42L43_SWIRE_DP1_CH3_INPUT:
	case CS42L43_SWIRE_DP1_CH4_INPUT:
	case CS42L43_SWIRE_DP2_CH1_INPUT:
	case CS42L43_SWIRE_DP2_CH2_INPUT:
	case CS42L43_SWIRE_DP3_CH1_INPUT:
	case CS42L43_SWIRE_DP3_CH2_INPUT:
	case CS42L43_SWIRE_DP4_CH1_INPUT:
	case CS42L43_SWIRE_DP4_CH2_INPUT:
	case CS42L43_ASRC_INT1_INPUT1:
	case CS42L43_ASRC_INT2_INPUT1:
	case CS42L43_ASRC_INT3_INPUT1:
	case CS42L43_ASRC_INT4_INPUT1:
	case CS42L43_ASRC_DEC1_INPUT1:
	case CS42L43_ASRC_DEC2_INPUT1:
	case CS42L43_ASRC_DEC3_INPUT1:
	case CS42L43_ASRC_DEC4_INPUT1:
	case CS42L43_ISRC1INT1_INPUT1:
	case CS42L43_ISRC1INT2_INPUT1:
	case CS42L43_ISRC1DEC1_INPUT1:
	case CS42L43_ISRC1DEC2_INPUT1:
	case CS42L43_ISRC2INT1_INPUT1:
	case CS42L43_ISRC2INT2_INPUT1:
	case CS42L43_ISRC2DEC1_INPUT1:
	case CS42L43_ISRC2DEC2_INPUT1:
	case CS42L43_EQ1MIX_INPUT1 ... CS42L43_EQ1MIX_INPUT4:
	case CS42L43_EQ2MIX_INPUT1 ... CS42L43_EQ2MIX_INPUT4:
	case CS42L43_SPDIF1_INPUT1:
	case CS42L43_SPDIF2_INPUT1:
	case CS42L43_AMP1MIX_INPUT1 ... CS42L43_AMP1MIX_INPUT4:
	case CS42L43_AMP2MIX_INPUT1 ... CS42L43_AMP2MIX_INPUT4:
	case CS42L43_AMP3MIX_INPUT1 ... CS42L43_AMP3MIX_INPUT4:
	case CS42L43_AMP4MIX_INPUT1 ... CS42L43_AMP4MIX_INPUT4:
	case CS42L43_ASRC_INT_ENABLES ... CS42L43_ASRC_DEC_ENABLES:
	case CS42L43_PDNCNTL:
	case CS42L43_RINGSENSE_DEB_CTRL:
	case CS42L43_TIPSENSE_DEB_CTRL:
	case CS42L43_TIP_RING_SENSE_INTERRUPT_STATUS:
	case CS42L43_HS2:
	case CS42L43_HS_STAT:
	case CS42L43_MCU_SW_INTERRUPT:
	case CS42L43_STEREO_MIC_CTRL:
	case CS42L43_STEREO_MIC_CLAMP_CTRL:
	case CS42L43_BLOCK_EN2 ... CS42L43_BLOCK_EN11:
	case CS42L43_TONE_CH1_CTRL ... CS42L43_TONE_CH2_CTRL:
	case CS42L43_MIC_DETECT_CONTROL_1:
	case CS42L43_DETECT_STATUS_1:
	case CS42L43_HS_BIAS_SENSE_AND_CLAMP_AUTOCONTROL:
	case CS42L43_MIC_DETECT_CONTROL_ANDROID:
	case CS42L43_ISRC1_CTRL:
	case CS42L43_ISRC2_CTRL:
	case CS42L43_CTRL_REG:
	case CS42L43_FDIV_FRAC:
	case CS42L43_CAL_RATIO:
	case CS42L43_SPI_CLK_CONFIG1:
	case CS42L43_SPI_CONFIG1 ... CS42L43_SPI_CONFIG4:
	case CS42L43_SPI_STATUS1 ... CS42L43_SPI_STATUS2:
	case CS42L43_TRAN_CONFIG1 ... CS42L43_TRAN_CONFIG8:
	case CS42L43_TRAN_STATUS1 ... CS42L43_TRAN_STATUS3:
	case CS42L43_TX_DATA:
	case CS42L43_RX_DATA:
	case CS42L43_DACCNFG1 ... CS42L43_DACCNFG2:
	case CS42L43_HPPATHVOL:
	case CS42L43_PGAVOL:
	case CS42L43_LOADDETRESULTS:
	case CS42L43_LOADDETENA:
	case CS42L43_CTRL:
	case CS42L43_COEFF_DATA_IN0:
	case CS42L43_COEFF_RD_WR0:
	case CS42L43_INIT_DONE0:
	case CS42L43_START_EQZ0:
	case CS42L43_MUTE_EQ_IN0:
	case CS42L43_DECIM_INT ... CS42L43_HPOUT_INT:
	case CS42L43_DECIM_MASK ... CS42L43_HPOUT_MASK:
	case CS42L43_DECIM_INT_SHADOW ... CS42L43_HP_OUT_SHADOW:
	case CS42L43_BOOT_CONTROL:
	case CS42L43_BLOCK_EN:
	case CS42L43_SHUTTER_CONTROL:
	case CS42L43_MCU_SW_REV ... CS42L43_MCU_RAM_MAX:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL_NS_GPL(cs42l43_readable_register, MFD_CS42L43);

bool cs42l43_precious_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS42L43_SFT_RESET:
	case CS42L43_TX_DATA:
	case CS42L43_RX_DATA:
	case CS42L43_DECIM_INT ... CS42L43_HPOUT_INT:
	case CS42L43_MCU_SW_REV ... CS42L43_MCU_RAM_MAX:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL_NS_GPL(cs42l43_precious_register, MFD_CS42L43);

bool cs42l43_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS42L43_DEVID:
	case CS42L43_REVID:
	case CS42L43_RELID:
	case CS42L43_GPIO_STS:
	case CS42L43_OTP_REVISION_ID:
	case CS42L43_TIP_RING_SENSE_INTERRUPT_STATUS:
	case CS42L43_HS_STAT:
	case CS42L43_MCU_SW_INTERRUPT:
	case CS42L43_DETECT_STATUS_1:
	case CS42L43_SPI_STATUS1 ... CS42L43_SPI_STATUS2:
	case CS42L43_TRAN_CONFIG1 ... CS42L43_TRAN_CONFIG2:
	case CS42L43_TRAN_CONFIG8:
	case CS42L43_TRAN_STATUS1 ... CS42L43_TRAN_STATUS3:
	case CS42L43_LOADDETRESULTS:
	case CS42L43_INIT_DONE0:
	case CS42L43_DECIM_INT_SHADOW ... CS42L43_HP_OUT_SHADOW:
	case CS42L43_BOOT_CONTROL:
	case CS42L43_BLOCK_EN:
		return true;
	default:
		return cs42l43_precious_register(dev, reg);
	}
}
EXPORT_SYMBOL_NS_GPL(cs42l43_volatile_register, MFD_CS42L43);

#define CS42L43_IRQ_OFFSET(reg) ((CS42L43_##reg##_INT) - CS42L43_DECIM_INT)

#define CS42L43_IRQ_REG(name, reg) REGMAP_IRQ_REG(CS42L43_##name, \
						  CS42L43_IRQ_OFFSET(reg), \
						  CS42L43_##name##_INT_MASK)

static const struct regmap_irq cs42l43_regmap_irqs[] = {
	CS42L43_IRQ_REG(PLL_LOST_LOCK,				PLL),
	CS42L43_IRQ_REG(PLL_READY,				PLL),

	CS42L43_IRQ_REG(HP_STARTUP_DONE,			MSM),
	CS42L43_IRQ_REG(HP_SHUTDOWN_DONE,			MSM),
	CS42L43_IRQ_REG(HSDET_DONE,				MSM),
	CS42L43_IRQ_REG(TIPSENSE_UNPLUG_DB,			MSM),
	CS42L43_IRQ_REG(TIPSENSE_PLUG_DB,			MSM),
	CS42L43_IRQ_REG(RINGSENSE_UNPLUG_DB,			MSM),
	CS42L43_IRQ_REG(RINGSENSE_PLUG_DB,			MSM),
	CS42L43_IRQ_REG(TIPSENSE_UNPLUG_PDET,			MSM),
	CS42L43_IRQ_REG(TIPSENSE_PLUG_PDET,			MSM),
	CS42L43_IRQ_REG(RINGSENSE_UNPLUG_PDET,			MSM),
	CS42L43_IRQ_REG(RINGSENSE_PLUG_PDET,			MSM),

	CS42L43_IRQ_REG(HS2_BIAS_SENSE,				ACC_DET),
	CS42L43_IRQ_REG(HS1_BIAS_SENSE,				ACC_DET),
	CS42L43_IRQ_REG(DC_DETECT1_FALSE,			ACC_DET),
	CS42L43_IRQ_REG(DC_DETECT1_TRUE,			ACC_DET),
	CS42L43_IRQ_REG(HSBIAS_CLAMPED,				ACC_DET),
	CS42L43_IRQ_REG(HS3_4_BIAS_SENSE,			ACC_DET),

	CS42L43_IRQ_REG(AMP2_CLK_STOP_FAULT,			CLASS_D_AMP),
	CS42L43_IRQ_REG(AMP1_CLK_STOP_FAULT,			CLASS_D_AMP),
	CS42L43_IRQ_REG(AMP2_VDDSPK_FAULT,			CLASS_D_AMP),
	CS42L43_IRQ_REG(AMP1_VDDSPK_FAULT,			CLASS_D_AMP),
	CS42L43_IRQ_REG(AMP2_SHUTDOWN_DONE,			CLASS_D_AMP),
	CS42L43_IRQ_REG(AMP1_SHUTDOWN_DONE,			CLASS_D_AMP),
	CS42L43_IRQ_REG(AMP2_STARTUP_DONE,			CLASS_D_AMP),
	CS42L43_IRQ_REG(AMP1_STARTUP_DONE,			CLASS_D_AMP),
	CS42L43_IRQ_REG(AMP2_THERM_SHDN,			CLASS_D_AMP),
	CS42L43_IRQ_REG(AMP1_THERM_SHDN,			CLASS_D_AMP),
	CS42L43_IRQ_REG(AMP2_THERM_WARN,			CLASS_D_AMP),
	CS42L43_IRQ_REG(AMP1_THERM_WARN,			CLASS_D_AMP),
	CS42L43_IRQ_REG(AMP2_SCDET,				CLASS_D_AMP),
	CS42L43_IRQ_REG(AMP1_SCDET,				CLASS_D_AMP),

	CS42L43_IRQ_REG(GPIO3_FALL,				GPIO),
	CS42L43_IRQ_REG(GPIO3_RISE,				GPIO),
	CS42L43_IRQ_REG(GPIO2_FALL,				GPIO),
	CS42L43_IRQ_REG(GPIO2_RISE,				GPIO),
	CS42L43_IRQ_REG(GPIO1_FALL,				GPIO),
	CS42L43_IRQ_REG(GPIO1_RISE,				GPIO),

	CS42L43_IRQ_REG(HP_ILIMIT,				HPOUT),
	CS42L43_IRQ_REG(HP_LOADDET_DONE,			HPOUT),
};

static const struct regmap_irq_chip cs42l43_irq_chip = {
	.name = "cs42l43",

	.status_base = CS42L43_DECIM_INT,
	.mask_base = CS42L43_DECIM_MASK,
	.num_regs = 16,

	.irqs = cs42l43_regmap_irqs,
	.num_irqs = ARRAY_SIZE(cs42l43_regmap_irqs),

	.runtime_pm = true,
};

static const char * const cs42l43_core_supplies[] = {
	"vdd-a", "vdd-io", "vdd-cp",
};

static const char * const cs42l43_parent_supplies[] = { "vdd-amp" };

static const struct mfd_cell cs42l43_devs[] = {
	{ .name = "cs42l43-pinctrl", },
	{ .name = "cs42l43-spi", },
	{
		.name = "cs42l43-codec",
		.parent_supplies = cs42l43_parent_supplies,
		.num_parent_supplies = ARRAY_SIZE(cs42l43_parent_supplies),
	},
};

/*
 * If the device is connected over Soundwire, as well as soft resetting the
 * device, this function will also way for the device to detach from the bus
 * before returning.
 */
static int cs42l43_soft_reset(struct cs42l43 *cs42l43)
{
	static const struct reg_sequence reset[] = {
		{ CS42L43_SFT_RESET, CS42L43_SFT_RESET_VAL },
	};

	reinit_completion(&cs42l43->device_detach);

	/*
	 * Apply cache only because the soft reset will cause the device to
	 * detach from the soundwire bus.
	 */
	regcache_cache_only(cs42l43->regmap, true);
	regmap_multi_reg_write_bypassed(cs42l43->regmap, reset, ARRAY_SIZE(reset));

	msleep(CS42L43_RESET_DELAY_MS);

	if (cs42l43->sdw) {
		unsigned long timeout = msecs_to_jiffies(CS42L43_SDW_DETACH_TIMEOUT_MS);
		unsigned long time;

		time = wait_for_completion_timeout(&cs42l43->device_detach, timeout);
		if (!time) {
			dev_err(cs42l43->dev, "Timed out waiting for device detach\n");
			return -ETIMEDOUT;
		}
	}

	return -EAGAIN;
}

/*
 * This function is essentially a no-op on I2C, but will wait for the device to
 * attach when the device is used on a SoundWire bus.
 */
static int cs42l43_wait_for_attach(struct cs42l43 *cs42l43)
{
	if (!cs42l43->attached) {
		unsigned long timeout = msecs_to_jiffies(CS42L43_SDW_ATTACH_TIMEOUT_MS);
		unsigned long time;

		time = wait_for_completion_timeout(&cs42l43->device_attach, timeout);
		if (!time) {
			dev_err(cs42l43->dev, "Timed out waiting for device re-attach\n");
			return -ETIMEDOUT;
		}
	}

	regcache_cache_only(cs42l43->regmap, false);

	/* The hardware requires enabling OSC_DIV before doing any SoundWire reads. */
	if (cs42l43->sdw)
		regmap_write(cs42l43->regmap, CS42L43_OSC_DIV_SEL,
			     CS42L43_OSC_DIV2_EN_MASK);

	return 0;
}

/*
 * This function will advance the firmware into boot stage 3 from boot stage 2.
 * Boot stage 3 is required to send commands to the firmware. This is achieved
 * by setting the firmware NEED configuration register to zero, this indicates
 * no configuration is required forcing the firmware to advance to boot stage 3.
 *
 * Later revisions of the firmware require the use of an alternative register
 * for this purpose, which is indicated through the shadow flag.
 */
static int cs42l43_mcu_stage_2_3(struct cs42l43 *cs42l43, bool shadow)
{
	unsigned int need_reg = CS42L43_NEED_CONFIGS;
	unsigned int val;
	int ret;

	if (shadow)
		need_reg = CS42L43_FW_SH_BOOT_CFG_NEED_CONFIGS;

	regmap_write(cs42l43->regmap, need_reg, 0);

	ret = regmap_read_poll_timeout(cs42l43->regmap, CS42L43_BOOT_STATUS,
				       val, (val == CS42L43_MCU_BOOT_STAGE3),
				       CS42L43_MCU_POLL_US, CS42L43_MCU_CMD_TIMEOUT_US);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to move to stage 3: %d, 0x%x\n", ret, val);
		return ret;
	}

	return -EAGAIN;
}

/*
 * This function will return the firmware to boot stage 2 from boot stage 3.
 * Boot stage 2 is required to apply updates to the firmware. This is achieved
 * by setting the firmware NEED configuration register to FW_PATCH_NEED_CFG,
 * setting the HAVE configuration register to 0, and soft resetting. The
 * firmware will see it is missing a patch configuration and will pause in boot
 * stage 2.
 *
 * Note: Unlike cs42l43_mcu_stage_2_3 there is no need to consider the shadow
 * register here as the driver will only return to boot stage 2 if the firmware
 * requires update which means the revision does not include shadow register
 * support.
 */
static int cs42l43_mcu_stage_3_2(struct cs42l43 *cs42l43)
{
	regmap_write(cs42l43->regmap, CS42L43_FW_MISSION_CTRL_NEED_CONFIGS,
		     CS42L43_FW_PATCH_NEED_CFG_MASK);
	regmap_write(cs42l43->regmap, CS42L43_FW_MISSION_CTRL_HAVE_CONFIGS, 0);

	return cs42l43_soft_reset(cs42l43);
}

/*
 * Disable the firmware running on the device such that the driver can access
 * the registers without fear of the MCU changing them under it.
 */
static int cs42l43_mcu_disable(struct cs42l43 *cs42l43)
{
	unsigned int val;
	int ret;

	regmap_write(cs42l43->regmap, CS42L43_FW_MISSION_CTRL_MM_MCU_CFG_REG,
		     CS42L43_FW_MISSION_CTRL_MM_MCU_CFG_DISABLE_VAL);
	regmap_write(cs42l43->regmap, CS42L43_FW_MISSION_CTRL_MM_CTRL_SELECTION,
		     CS42L43_FW_MM_CTRL_MCU_SEL_MASK);
	regmap_write(cs42l43->regmap, CS42L43_MCU_SW_INTERRUPT, CS42L43_CONTROL_IND_MASK);
	regmap_write(cs42l43->regmap, CS42L43_MCU_SW_INTERRUPT, 0);

	ret = regmap_read_poll_timeout(cs42l43->regmap, CS42L43_SOFT_INT_SHADOW, val,
				       (val & CS42L43_CONTROL_APPLIED_INT_MASK),
				       CS42L43_MCU_POLL_US, CS42L43_MCU_CMD_TIMEOUT_US);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to disable firmware: %d, 0x%x\n", ret, val);
		return ret;
	}

	/* Soft reset to clear any register state the firmware left behind. */
	return cs42l43_soft_reset(cs42l43);
}

/*
 * Callback to load firmware updates.
 */
static void cs42l43_mcu_load_firmware(const struct firmware *firmware, void *context)
{
	struct cs42l43 *cs42l43 = context;
	const struct cs42l43_patch_header *hdr;
	unsigned int loadaddr, val;
	int ret;

	if (!firmware) {
		dev_err(cs42l43->dev, "Failed to load firmware\n");
		cs42l43->firmware_error = -ENODEV;
		goto err;
	}

	hdr = (const struct cs42l43_patch_header *)&firmware->data[0];
	loadaddr = le32_to_cpu(hdr->load_addr);

	if (le16_to_cpu(hdr->version) != CS42L43_MCU_UPDATE_FORMAT) {
		dev_err(cs42l43->dev, "Bad firmware file format: %d\n", hdr->version);
		cs42l43->firmware_error = -EINVAL;
		goto err_release;
	}

	regmap_write(cs42l43->regmap, CS42L43_PATCH_START_ADDR, loadaddr);
	regmap_bulk_write(cs42l43->regmap, loadaddr + CS42L43_MCU_UPDATE_OFFSET,
			  &firmware->data[0], firmware->size / sizeof(u32));

	regmap_write(cs42l43->regmap, CS42L43_MCU_SW_INTERRUPT, CS42L43_PATCH_IND_MASK);
	regmap_write(cs42l43->regmap, CS42L43_MCU_SW_INTERRUPT, 0);

	ret = regmap_read_poll_timeout(cs42l43->regmap, CS42L43_SOFT_INT_SHADOW, val,
				       (val & CS42L43_PATCH_APPLIED_INT_MASK),
				       CS42L43_MCU_POLL_US, CS42L43_MCU_UPDATE_TIMEOUT_US);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to update firmware: %d, 0x%x\n", ret, val);
		cs42l43->firmware_error = ret;
		goto err_release;
	}

err_release:
	release_firmware(firmware);
err:
	complete(&cs42l43->firmware_download);
}

static int cs42l43_mcu_is_hw_compatible(struct cs42l43 *cs42l43,
					unsigned int mcu_rev,
					unsigned int bios_rev)
{
	/*
	 * The firmware has two revision numbers bringing either of them up to a
	 * supported version will provide the disable the driver requires.
	 */
	if (mcu_rev < CS42L43_MCU_SUPPORTED_REV &&
	    bios_rev < CS42L43_MCU_SUPPORTED_BIOS_REV) {
		dev_err(cs42l43->dev, "Firmware too old to support disable\n");
		return -EINVAL;
	}

	return 0;
}

/*
 * The process of updating the firmware is split into a series of steps, at the
 * end of each step a soft reset of the device might be required which will
 * require the driver to wait for the device to re-attach on the SoundWire bus,
 * if that control bus is being used.
 */
static int cs42l43_mcu_update_step(struct cs42l43 *cs42l43)
{
	unsigned int mcu_rev, bios_rev, boot_status, secure_cfg;
	bool patched, shadow;
	int ret;

	/* Clear any stale software interrupt bits. */
	regmap_read(cs42l43->regmap, CS42L43_SOFT_INT, &mcu_rev);

	ret = regmap_read(cs42l43->regmap, CS42L43_BOOT_STATUS, &boot_status);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to read boot status: %d\n", ret);
		return ret;
	}

	ret = regmap_read(cs42l43->regmap, CS42L43_MCU_SW_REV, &mcu_rev);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to read firmware revision: %d\n", ret);
		return ret;
	}

	bios_rev = (((mcu_rev & CS42L43_BIOS_MAJOR_REV_MASK) << 12) |
		    ((mcu_rev & CS42L43_BIOS_MINOR_REV_MASK) << 4) |
		    ((mcu_rev & CS42L43_BIOS_SUBMINOR_REV_MASK) >> 8)) >>
		   CS42L43_BIOS_MAJOR_REV_SHIFT;
	mcu_rev = ((mcu_rev & CS42L43_FW_MAJOR_REV_MASK) << 12) |
		  ((mcu_rev & CS42L43_FW_MINOR_REV_MASK) << 4) |
		  ((mcu_rev & CS42L43_FW_SUBMINOR_REV_MASK) >> 8);

	/*
	 * The firmware has two revision numbers both of them being at the ROM
	 * revision indicates no patch has been applied.
	 */
	patched = mcu_rev != CS42L43_MCU_ROM_REV || bios_rev != CS42L43_MCU_ROM_BIOS_REV;
	/*
	 * Later versions of the firmwware require the driver to access some
	 * features through a set of shadow registers.
	 */
	shadow = mcu_rev >= CS42L43_MCU_SHADOW_REGS_REQUIRED_REV;

	ret = regmap_read(cs42l43->regmap, CS42L43_BOOT_CONTROL, &secure_cfg);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to read security settings: %d\n", ret);
		return ret;
	}

	cs42l43->hw_lock = secure_cfg & CS42L43_LOCK_HW_STS_MASK;

	if (!patched && cs42l43->hw_lock) {
		dev_err(cs42l43->dev, "Unpatched secure device\n");
		return -EPERM;
	}

	dev_dbg(cs42l43->dev, "Firmware(0x%x, 0x%x) in boot stage %d\n",
		mcu_rev, bios_rev, boot_status);

	switch (boot_status) {
	case CS42L43_MCU_BOOT_STAGE2:
		if (!patched) {
			ret = request_firmware_nowait(THIS_MODULE, FW_ACTION_UEVENT,
						      "cs42l43.bin", cs42l43->dev,
						      GFP_KERNEL, cs42l43,
						      cs42l43_mcu_load_firmware);
			if (ret) {
				dev_err(cs42l43->dev, "Failed to request firmware: %d\n", ret);
				return ret;
			}

			wait_for_completion(&cs42l43->firmware_download);

			if (cs42l43->firmware_error)
				return cs42l43->firmware_error;

			return -EAGAIN;
		} else {
			return cs42l43_mcu_stage_2_3(cs42l43, shadow);
		}
	case CS42L43_MCU_BOOT_STAGE3:
		if (patched) {
			ret = cs42l43_mcu_is_hw_compatible(cs42l43, mcu_rev, bios_rev);
			if (ret)
				return ret;

			return cs42l43_mcu_disable(cs42l43);
		} else {
			return cs42l43_mcu_stage_3_2(cs42l43);
		}
	case CS42L43_MCU_BOOT_STAGE4:
		return 0;
	default:
		dev_err(cs42l43->dev, "Invalid boot status: %d\n", boot_status);
		return -EINVAL;
	}
}

/*
 * Update the firmware running on the device.
 */
static int cs42l43_mcu_update(struct cs42l43 *cs42l43)
{
	int i, ret;

	for (i = 0; i < CS42L43_MCU_UPDATE_RETRIES; i++) {
		ret = cs42l43_mcu_update_step(cs42l43);
		if (ret != -EAGAIN)
			return ret;

		ret = cs42l43_wait_for_attach(cs42l43);
		if (ret)
			return ret;
	}

	dev_err(cs42l43->dev, "Failed retrying update\n");
	return -ETIMEDOUT;
}

static int cs42l43_irq_config(struct cs42l43 *cs42l43)
{
	struct irq_data *irq_data;
	unsigned long irq_flags;
	int ret;

	if (cs42l43->sdw)
		cs42l43->irq = cs42l43->sdw->irq;

	cs42l43->irq_chip = cs42l43_irq_chip;
	cs42l43->irq_chip.irq_drv_data = cs42l43;

	irq_data = irq_get_irq_data(cs42l43->irq);
	if (!irq_data) {
		dev_err(cs42l43->dev, "Invalid IRQ: %d\n", cs42l43->irq);
		return -EINVAL;
	}

	irq_flags = irqd_get_trigger_type(irq_data);
	switch (irq_flags) {
	case IRQF_TRIGGER_LOW:
	case IRQF_TRIGGER_HIGH:
	case IRQF_TRIGGER_RISING:
	case IRQF_TRIGGER_FALLING:
		break;
	case IRQ_TYPE_NONE:
	default:
		irq_flags = IRQF_TRIGGER_LOW;
		break;
	}

	irq_flags |= IRQF_ONESHOT;

	ret = devm_regmap_add_irq_chip(cs42l43->dev, cs42l43->regmap,
				       cs42l43->irq, irq_flags, 0,
				       &cs42l43->irq_chip, &cs42l43->irq_data);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to add IRQ chip: %d\n", ret);
		return ret;
	}

	dev_dbg(cs42l43->dev, "Configured IRQ %d with flags 0x%lx\n",
		cs42l43->irq, irq_flags);

	return 0;
}

static void cs42l43_boot_work(struct work_struct *work)
{
	struct cs42l43 *cs42l43 = container_of(work, struct cs42l43, boot_work);
	unsigned int devid, revid, otp;
	int ret;

	ret = cs42l43_wait_for_attach(cs42l43);
	if (ret)
		goto err;

	ret = regmap_read(cs42l43->regmap, CS42L43_DEVID, &devid);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to read devid: %d\n", ret);
		goto err;
	}

	switch (devid) {
	case CS42L43_DEVID_VAL:
		break;
	default:
		dev_err(cs42l43->dev, "Unrecognised devid: 0x%06x\n", devid);
		goto err;
	}

	ret = regmap_read(cs42l43->regmap, CS42L43_REVID, &revid);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to read rev: %d\n", ret);
		goto err;
	}

	ret = regmap_read(cs42l43->regmap, CS42L43_OTP_REVISION_ID, &otp);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to read otp rev: %d\n", ret);
		goto err;
	}

	dev_info(cs42l43->dev,
		 "devid: 0x%06x, rev: 0x%02x, otp: 0x%02x\n", devid, revid, otp);

	ret = cs42l43_mcu_update(cs42l43);
	if (ret)
		goto err;

	ret = regmap_register_patch(cs42l43->regmap, cs42l43_reva_patch,
				    ARRAY_SIZE(cs42l43_reva_patch));
	if (ret) {
		dev_err(cs42l43->dev, "Failed to apply register patch: %d\n", ret);
		goto err;
	}

	ret = cs42l43_irq_config(cs42l43);
	if (ret)
		goto err;

	ret = devm_mfd_add_devices(cs42l43->dev, PLATFORM_DEVID_NONE,
				   cs42l43_devs, ARRAY_SIZE(cs42l43_devs),
				   NULL, 0, NULL);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to add subdevices: %d\n", ret);
		goto err;
	}

	pm_runtime_mark_last_busy(cs42l43->dev);
	pm_runtime_put_autosuspend(cs42l43->dev);

	return;

err:
	pm_runtime_put_sync(cs42l43->dev);
	cs42l43_dev_remove(cs42l43);
}

static int cs42l43_power_up(struct cs42l43 *cs42l43)
{
	int ret;

	ret = regulator_enable(cs42l43->vdd_p);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to enable vdd-p: %d\n", ret);
		return ret;
	}

	/* vdd-p must be on for 50uS before any other supply */
	usleep_range(CS42L43_VDDP_DELAY_US, 2 * CS42L43_VDDP_DELAY_US);

	gpiod_set_value_cansleep(cs42l43->reset, 1);

	ret = regulator_bulk_enable(CS42L43_N_SUPPLIES, cs42l43->core_supplies);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to enable core supplies: %d\n", ret);
		goto err_reset;
	}

	ret = regulator_enable(cs42l43->vdd_d);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to enable vdd-d: %d\n", ret);
		goto err_core_supplies;
	}

	usleep_range(CS42L43_VDDD_DELAY_US, 2 * CS42L43_VDDD_DELAY_US);

	return 0;

err_core_supplies:
	regulator_bulk_disable(CS42L43_N_SUPPLIES, cs42l43->core_supplies);
err_reset:
	gpiod_set_value_cansleep(cs42l43->reset, 0);
	regulator_disable(cs42l43->vdd_p);

	return ret;
}

static int cs42l43_power_down(struct cs42l43 *cs42l43)
{
	int ret;

	ret = regulator_disable(cs42l43->vdd_d);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to disable vdd-d: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_disable(CS42L43_N_SUPPLIES, cs42l43->core_supplies);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to disable core supplies: %d\n", ret);
		return ret;
	}

	gpiod_set_value_cansleep(cs42l43->reset, 0);

	ret = regulator_disable(cs42l43->vdd_p);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to disable vdd-p: %d\n", ret);
		return ret;
	}

	return 0;
}

int cs42l43_dev_probe(struct cs42l43 *cs42l43)
{
	int i, ret;

	dev_set_drvdata(cs42l43->dev, cs42l43);

	mutex_init(&cs42l43->pll_lock);
	init_completion(&cs42l43->device_attach);
	init_completion(&cs42l43->device_detach);
	init_completion(&cs42l43->firmware_download);
	INIT_WORK(&cs42l43->boot_work, cs42l43_boot_work);

	regcache_cache_only(cs42l43->regmap, true);

	cs42l43->reset = devm_gpiod_get_optional(cs42l43->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(cs42l43->reset))
		return dev_err_probe(cs42l43->dev, PTR_ERR(cs42l43->reset),
				     "Failed to get reset\n");

	cs42l43->vdd_p = devm_regulator_get(cs42l43->dev, "vdd-p");
	if (IS_ERR(cs42l43->vdd_p))
		return dev_err_probe(cs42l43->dev, PTR_ERR(cs42l43->vdd_p),
				     "Failed to get vdd-p\n");

	cs42l43->vdd_d = devm_regulator_get(cs42l43->dev, "vdd-d");
	if (IS_ERR(cs42l43->vdd_d))
		return dev_err_probe(cs42l43->dev, PTR_ERR(cs42l43->vdd_d),
				     "Failed to get vdd-d\n");

	BUILD_BUG_ON(ARRAY_SIZE(cs42l43_core_supplies) != CS42L43_N_SUPPLIES);

	for (i = 0; i < CS42L43_N_SUPPLIES; i++)
		cs42l43->core_supplies[i].supply = cs42l43_core_supplies[i];

	ret = devm_regulator_bulk_get(cs42l43->dev, CS42L43_N_SUPPLIES,
				      cs42l43->core_supplies);
	if (ret)
		return dev_err_probe(cs42l43->dev, ret,
				     "Failed to get core supplies\n");

	ret = cs42l43_power_up(cs42l43);
	if (ret)
		return ret;

	pm_runtime_set_autosuspend_delay(cs42l43->dev, CS42L43_AUTOSUSPEND_TIME_MS);
	pm_runtime_use_autosuspend(cs42l43->dev);
	pm_runtime_set_active(cs42l43->dev);
	/*
	 * The device is already powered up, but keep it from suspending until
	 * the boot work runs.
	 */
	pm_runtime_get_noresume(cs42l43->dev);
	ret = devm_pm_runtime_enable(cs42l43->dev);
	if (ret)
		return ret;

	queue_work(system_long_wq, &cs42l43->boot_work);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs42l43_dev_probe, MFD_CS42L43);

void cs42l43_dev_remove(struct cs42l43 *cs42l43)
{
	cs42l43_power_down(cs42l43);
}
EXPORT_SYMBOL_NS_GPL(cs42l43_dev_remove, MFD_CS42L43);

static int cs42l43_suspend(struct device *dev)
{
	struct cs42l43 *cs42l43 = dev_get_drvdata(dev);
	int ret;

	/*
	 * Don't care about being resumed here, but the driver does want
	 * force_resume to always trigger an actual resume, so that register
	 * state for the MCU/GPIOs is returned as soon as possible after system
	 * resume. force_resume will resume if the reference count is resumed on
	 * suspend hence the get_noresume.
	 */
	pm_runtime_get_noresume(dev);

	ret = pm_runtime_force_suspend(dev);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to force suspend: %d\n", ret);
		pm_runtime_put_noidle(dev);
		return ret;
	}

	pm_runtime_put_noidle(dev);

	ret = cs42l43_power_down(cs42l43);
	if (ret)
		return ret;

	return 0;
}

static int cs42l43_resume(struct device *dev)
{
	struct cs42l43 *cs42l43 = dev_get_drvdata(dev);
	int ret;

	ret = cs42l43_power_up(cs42l43);
	if (ret)
		return ret;

	ret = pm_runtime_force_resume(dev);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to force resume: %d\n", ret);
		return ret;
	}

	return 0;
}

static int cs42l43_runtime_suspend(struct device *dev)
{
	struct cs42l43 *cs42l43 = dev_get_drvdata(dev);

	/*
	 * Whilst the driver doesn't power the chip down here, going into runtime
	 * suspend lets the SoundWire bus power down, which means the driver
	 * can't communicate with the device any more.
	 */
	regcache_cache_only(cs42l43->regmap, true);

	return 0;
}

static int cs42l43_runtime_resume(struct device *dev)
{
	struct cs42l43 *cs42l43 = dev_get_drvdata(dev);
	unsigned int reset_canary;
	int ret;

	ret = cs42l43_wait_for_attach(cs42l43);
	if (ret)
		return ret;

	ret = regmap_read(cs42l43->regmap, CS42L43_RELID, &reset_canary);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to check reset canary: %d\n", ret);
		goto err;
	}

	if (!reset_canary) {
		/*
		 * If the canary has cleared the chip has reset, re-handle the
		 * MCU and mark the cache as dirty to indicate the chip reset.
		 */
		ret = cs42l43_mcu_update(cs42l43);
		if (ret)
			goto err;

		regcache_mark_dirty(cs42l43->regmap);
	}

	ret = regcache_sync(cs42l43->regmap);
	if (ret) {
		dev_err(cs42l43->dev, "Failed to restore register cache: %d\n", ret);
		goto err;
	}

	return 0;

err:
	regcache_cache_only(cs42l43->regmap, true);

	return ret;
}

EXPORT_NS_GPL_DEV_PM_OPS(cs42l43_pm_ops, MFD_CS42L43) = {
	SYSTEM_SLEEP_PM_OPS(cs42l43_suspend, cs42l43_resume)
	RUNTIME_PM_OPS(cs42l43_runtime_suspend, cs42l43_runtime_resume, NULL)
};

MODULE_DESCRIPTION("CS42L43 Core Driver");
MODULE_AUTHOR("Charles Keepax <ckeepax@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("cs42l43.bin");
