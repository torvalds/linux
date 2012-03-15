/*
 * ti_hdmi_4xxx_ip.h
 *
 * HDMI header definition for DM81xx, DM38xx, TI OMAP4 etc processors.
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _HDMI_TI_4xxx_H_
#define _HDMI_TI_4xxx_H_

#include <linux/string.h>
#include <video/omapdss.h>
#include "ti_hdmi.h"

/* HDMI Wrapper */

#define HDMI_WP_REVISION			0x0
#define HDMI_WP_SYSCONFIG			0x10
#define HDMI_WP_IRQSTATUS_RAW			0x24
#define HDMI_WP_IRQSTATUS			0x28
#define HDMI_WP_PWR_CTRL			0x40
#define HDMI_WP_IRQENABLE_SET			0x2C
#define HDMI_WP_VIDEO_CFG			0x50
#define HDMI_WP_VIDEO_SIZE			0x60
#define HDMI_WP_VIDEO_TIMING_H			0x68
#define HDMI_WP_VIDEO_TIMING_V			0x6C
#define HDMI_WP_WP_CLK				0x70
#define HDMI_WP_AUDIO_CFG			0x80
#define HDMI_WP_AUDIO_CFG2			0x84
#define HDMI_WP_AUDIO_CTRL			0x88
#define HDMI_WP_AUDIO_DATA			0x8C

/* HDMI IP Core System */

#define HDMI_CORE_SYS_VND_IDL			0x0
#define HDMI_CORE_SYS_DEV_IDL			0x8
#define HDMI_CORE_SYS_DEV_IDH			0xC
#define HDMI_CORE_SYS_DEV_REV			0x10
#define HDMI_CORE_SYS_SRST			0x14
#define HDMI_CORE_CTRL1				0x20
#define HDMI_CORE_SYS_SYS_STAT			0x24
#define HDMI_CORE_SYS_DE_DLY			0xC8
#define HDMI_CORE_SYS_DE_CTRL			0xCC
#define HDMI_CORE_SYS_DE_TOP			0xD0
#define HDMI_CORE_SYS_DE_CNTL			0xD8
#define HDMI_CORE_SYS_DE_CNTH			0xDC
#define HDMI_CORE_SYS_DE_LINL			0xE0
#define HDMI_CORE_SYS_DE_LINH_1			0xE4
#define HDMI_CORE_SYS_VID_ACEN			0x124
#define HDMI_CORE_SYS_VID_MODE			0x128
#define HDMI_CORE_SYS_INTR_STATE		0x1C0
#define HDMI_CORE_SYS_INTR1			0x1C4
#define HDMI_CORE_SYS_INTR2			0x1C8
#define HDMI_CORE_SYS_INTR3			0x1CC
#define HDMI_CORE_SYS_INTR4			0x1D0
#define HDMI_CORE_SYS_UMASK1			0x1D4
#define HDMI_CORE_SYS_TMDS_CTRL			0x208

#define HDMI_CORE_CTRL1_VEN_FOLLOWVSYNC	0x1
#define HDMI_CORE_CTRL1_HEN_FOLLOWHSYNC	0x1
#define HDMI_CORE_CTRL1_BSEL_24BITBUS	0x1
#define HDMI_CORE_CTRL1_EDGE_RISINGEDGE	0x1

/* HDMI DDC E-DID */
#define HDMI_CORE_DDC_ADDR			0x3B4
#define HDMI_CORE_DDC_SEGM			0x3B8
#define HDMI_CORE_DDC_OFFSET			0x3BC
#define HDMI_CORE_DDC_COUNT1			0x3C0
#define HDMI_CORE_DDC_COUNT2			0x3C4
#define HDMI_CORE_DDC_STATUS			0x3C8
#define HDMI_CORE_DDC_CMD			0x3CC
#define HDMI_CORE_DDC_DATA			0x3D0

/* HDMI IP Core Audio Video */

#define HDMI_CORE_AV_ACR_CTRL			0x4
#define HDMI_CORE_AV_FREQ_SVAL			0x8
#define HDMI_CORE_AV_N_SVAL1			0xC
#define HDMI_CORE_AV_N_SVAL2			0x10
#define HDMI_CORE_AV_N_SVAL3			0x14
#define HDMI_CORE_AV_CTS_SVAL1			0x18
#define HDMI_CORE_AV_CTS_SVAL2			0x1C
#define HDMI_CORE_AV_CTS_SVAL3			0x20
#define HDMI_CORE_AV_CTS_HVAL1			0x24
#define HDMI_CORE_AV_CTS_HVAL2			0x28
#define HDMI_CORE_AV_CTS_HVAL3			0x2C
#define HDMI_CORE_AV_AUD_MODE			0x50
#define HDMI_CORE_AV_SPDIF_CTRL			0x54
#define HDMI_CORE_AV_HW_SPDIF_FS		0x60
#define HDMI_CORE_AV_SWAP_I2S			0x64
#define HDMI_CORE_AV_SPDIF_ERTH			0x6C
#define HDMI_CORE_AV_I2S_IN_MAP			0x70
#define HDMI_CORE_AV_I2S_IN_CTRL		0x74
#define HDMI_CORE_AV_I2S_CHST0			0x78
#define HDMI_CORE_AV_I2S_CHST1			0x7C
#define HDMI_CORE_AV_I2S_CHST2			0x80
#define HDMI_CORE_AV_I2S_CHST4			0x84
#define HDMI_CORE_AV_I2S_CHST5			0x88
#define HDMI_CORE_AV_ASRC			0x8C
#define HDMI_CORE_AV_I2S_IN_LEN			0x90
#define HDMI_CORE_AV_HDMI_CTRL			0xBC
#define HDMI_CORE_AV_AUDO_TXSTAT		0xC0
#define HDMI_CORE_AV_AUD_PAR_BUSCLK_1		0xCC
#define HDMI_CORE_AV_AUD_PAR_BUSCLK_2		0xD0
#define HDMI_CORE_AV_AUD_PAR_BUSCLK_3		0xD4
#define HDMI_CORE_AV_TEST_TXCTRL		0xF0
#define HDMI_CORE_AV_DPD			0xF4
#define HDMI_CORE_AV_PB_CTRL1			0xF8
#define HDMI_CORE_AV_PB_CTRL2			0xFC
#define HDMI_CORE_AV_AVI_TYPE			0x100
#define HDMI_CORE_AV_AVI_VERS			0x104
#define HDMI_CORE_AV_AVI_LEN			0x108
#define HDMI_CORE_AV_AVI_CHSUM			0x10C
#define HDMI_CORE_AV_AVI_DBYTE(n)		(n * 4 + 0x110)
#define HDMI_CORE_AV_SPD_TYPE			0x180
#define HDMI_CORE_AV_SPD_VERS			0x184
#define HDMI_CORE_AV_SPD_LEN			0x188
#define HDMI_CORE_AV_SPD_CHSUM			0x18C
#define HDMI_CORE_AV_SPD_DBYTE(n)		(n * 4 + 0x190)
#define HDMI_CORE_AV_AUDIO_TYPE			0x200
#define HDMI_CORE_AV_AUDIO_VERS			0x204
#define HDMI_CORE_AV_AUDIO_LEN			0x208
#define HDMI_CORE_AV_AUDIO_CHSUM		0x20C
#define HDMI_CORE_AV_AUD_DBYTE(n)		(n * 4 + 0x210)
#define HDMI_CORE_AV_MPEG_TYPE			0x280
#define HDMI_CORE_AV_MPEG_VERS			0x284
#define HDMI_CORE_AV_MPEG_LEN			0x288
#define HDMI_CORE_AV_MPEG_CHSUM			0x28C
#define HDMI_CORE_AV_MPEG_DBYTE(n)		(n * 4 + 0x290)
#define HDMI_CORE_AV_GEN_DBYTE(n)		(n * 4 + 0x300)
#define HDMI_CORE_AV_CP_BYTE1			0x37C
#define HDMI_CORE_AV_GEN2_DBYTE(n)		(n * 4 + 0x380)
#define HDMI_CORE_AV_CEC_ADDR_ID		0x3FC

#define HDMI_CORE_AV_SPD_DBYTE_ELSIZE		0x4
#define HDMI_CORE_AV_GEN2_DBYTE_ELSIZE		0x4
#define HDMI_CORE_AV_MPEG_DBYTE_ELSIZE		0x4
#define HDMI_CORE_AV_GEN_DBYTE_ELSIZE		0x4

#define HDMI_CORE_AV_AVI_DBYTE_NELEMS		15
#define HDMI_CORE_AV_SPD_DBYTE_NELEMS		27
#define HDMI_CORE_AV_AUD_DBYTE_NELEMS		10
#define HDMI_CORE_AV_MPEG_DBYTE_NELEMS		27
#define HDMI_CORE_AV_GEN_DBYTE_NELEMS		31
#define HDMI_CORE_AV_GEN2_DBYTE_NELEMS		31

/* PLL */

#define PLLCTRL_PLL_CONTROL			0x0
#define PLLCTRL_PLL_STATUS			0x4
#define PLLCTRL_PLL_GO				0x8
#define PLLCTRL_CFG1				0xC
#define PLLCTRL_CFG2				0x10
#define PLLCTRL_CFG3				0x14
#define PLLCTRL_CFG4				0x20

/* HDMI PHY */

#define HDMI_TXPHY_TX_CTRL			0x0
#define HDMI_TXPHY_DIGITAL_CTRL			0x4
#define HDMI_TXPHY_POWER_CTRL			0x8
#define HDMI_TXPHY_PAD_CFG_CTRL			0xC

#define REG_FLD_MOD(base, idx, val, start, end) \
	hdmi_write_reg(base, idx, FLD_MOD(hdmi_read_reg(base, idx),\
							val, start, end))
#define REG_GET(base, idx, start, end) \
	FLD_GET(hdmi_read_reg(base, idx), start, end)

enum hdmi_phy_pwr {
	HDMI_PHYPWRCMD_OFF = 0,
	HDMI_PHYPWRCMD_LDOON = 1,
	HDMI_PHYPWRCMD_TXON = 2
};

enum hdmi_core_inputbus_width {
	HDMI_INPUT_8BIT = 0,
	HDMI_INPUT_10BIT = 1,
	HDMI_INPUT_12BIT = 2
};

enum hdmi_core_dither_trunc {
	HDMI_OUTPUTTRUNCATION_8BIT = 0,
	HDMI_OUTPUTTRUNCATION_10BIT = 1,
	HDMI_OUTPUTTRUNCATION_12BIT = 2,
	HDMI_OUTPUTDITHER_8BIT = 3,
	HDMI_OUTPUTDITHER_10BIT = 4,
	HDMI_OUTPUTDITHER_12BIT = 5
};

enum hdmi_core_deepcolor_ed {
	HDMI_DEEPCOLORPACKECTDISABLE = 0,
	HDMI_DEEPCOLORPACKECTENABLE = 1
};

enum hdmi_core_packet_mode {
	HDMI_PACKETMODERESERVEDVALUE = 0,
	HDMI_PACKETMODE24BITPERPIXEL = 4,
	HDMI_PACKETMODE30BITPERPIXEL = 5,
	HDMI_PACKETMODE36BITPERPIXEL = 6,
	HDMI_PACKETMODE48BITPERPIXEL = 7
};

enum hdmi_core_tclkselclkmult {
	HDMI_FPLL05IDCK = 0,
	HDMI_FPLL10IDCK = 1,
	HDMI_FPLL20IDCK = 2,
	HDMI_FPLL40IDCK = 3
};

enum hdmi_core_packet_ctrl {
	HDMI_PACKETENABLE = 1,
	HDMI_PACKETDISABLE = 0,
	HDMI_PACKETREPEATON = 1,
	HDMI_PACKETREPEATOFF = 0
};

/* INFOFRAME_AVI_ and INFOFRAME_AUDIO_ definitions */
enum hdmi_core_infoframe {
	HDMI_INFOFRAME_AVI_DB1Y_RGB = 0,
	HDMI_INFOFRAME_AVI_DB1Y_YUV422 = 1,
	HDMI_INFOFRAME_AVI_DB1Y_YUV444 = 2,
	HDMI_INFOFRAME_AVI_DB1A_ACTIVE_FORMAT_OFF = 0,
	HDMI_INFOFRAME_AVI_DB1A_ACTIVE_FORMAT_ON =  1,
	HDMI_INFOFRAME_AVI_DB1B_NO = 0,
	HDMI_INFOFRAME_AVI_DB1B_VERT = 1,
	HDMI_INFOFRAME_AVI_DB1B_HORI = 2,
	HDMI_INFOFRAME_AVI_DB1B_VERTHORI = 3,
	HDMI_INFOFRAME_AVI_DB1S_0 = 0,
	HDMI_INFOFRAME_AVI_DB1S_1 = 1,
	HDMI_INFOFRAME_AVI_DB1S_2 = 2,
	HDMI_INFOFRAME_AVI_DB2C_NO = 0,
	HDMI_INFOFRAME_AVI_DB2C_ITU601 = 1,
	HDMI_INFOFRAME_AVI_DB2C_ITU709 = 2,
	HDMI_INFOFRAME_AVI_DB2C_EC_EXTENDED = 3,
	HDMI_INFOFRAME_AVI_DB2M_NO = 0,
	HDMI_INFOFRAME_AVI_DB2M_43 = 1,
	HDMI_INFOFRAME_AVI_DB2M_169 = 2,
	HDMI_INFOFRAME_AVI_DB2R_SAME = 8,
	HDMI_INFOFRAME_AVI_DB2R_43 = 9,
	HDMI_INFOFRAME_AVI_DB2R_169 = 10,
	HDMI_INFOFRAME_AVI_DB2R_149 = 11,
	HDMI_INFOFRAME_AVI_DB3ITC_NO = 0,
	HDMI_INFOFRAME_AVI_DB3ITC_YES = 1,
	HDMI_INFOFRAME_AVI_DB3EC_XVYUV601 = 0,
	HDMI_INFOFRAME_AVI_DB3EC_XVYUV709 = 1,
	HDMI_INFOFRAME_AVI_DB3Q_DEFAULT = 0,
	HDMI_INFOFRAME_AVI_DB3Q_LR = 1,
	HDMI_INFOFRAME_AVI_DB3Q_FR = 2,
	HDMI_INFOFRAME_AVI_DB3SC_NO = 0,
	HDMI_INFOFRAME_AVI_DB3SC_HORI = 1,
	HDMI_INFOFRAME_AVI_DB3SC_VERT = 2,
	HDMI_INFOFRAME_AVI_DB3SC_HORIVERT = 3,
	HDMI_INFOFRAME_AVI_DB5PR_NO = 0,
	HDMI_INFOFRAME_AVI_DB5PR_2 = 1,
	HDMI_INFOFRAME_AVI_DB5PR_3 = 2,
	HDMI_INFOFRAME_AVI_DB5PR_4 = 3,
	HDMI_INFOFRAME_AVI_DB5PR_5 = 4,
	HDMI_INFOFRAME_AVI_DB5PR_6 = 5,
	HDMI_INFOFRAME_AVI_DB5PR_7 = 6,
	HDMI_INFOFRAME_AVI_DB5PR_8 = 7,
	HDMI_INFOFRAME_AVI_DB5PR_9 = 8,
	HDMI_INFOFRAME_AVI_DB5PR_10 = 9,
	HDMI_INFOFRAME_AUDIO_DB1CT_FROM_STREAM = 0,
	HDMI_INFOFRAME_AUDIO_DB1CT_IEC60958 = 1,
	HDMI_INFOFRAME_AUDIO_DB1CT_AC3 = 2,
	HDMI_INFOFRAME_AUDIO_DB1CT_MPEG1 = 3,
	HDMI_INFOFRAME_AUDIO_DB1CT_MP3 = 4,
	HDMI_INFOFRAME_AUDIO_DB1CT_MPEG2_MULTICH = 5,
	HDMI_INFOFRAME_AUDIO_DB1CT_AAC = 6,
	HDMI_INFOFRAME_AUDIO_DB1CT_DTS = 7,
	HDMI_INFOFRAME_AUDIO_DB1CT_ATRAC = 8,
	HDMI_INFOFRAME_AUDIO_DB1CT_ONEBIT = 9,
	HDMI_INFOFRAME_AUDIO_DB1CT_DOLBY_DIGITAL_PLUS = 10,
	HDMI_INFOFRAME_AUDIO_DB1CT_DTS_HD = 11,
	HDMI_INFOFRAME_AUDIO_DB1CT_MAT = 12,
	HDMI_INFOFRAME_AUDIO_DB1CT_DST = 13,
	HDMI_INFOFRAME_AUDIO_DB1CT_WMA_PRO = 14,
	HDMI_INFOFRAME_AUDIO_DB2SF_FROM_STREAM = 0,
	HDMI_INFOFRAME_AUDIO_DB2SF_32000 = 1,
	HDMI_INFOFRAME_AUDIO_DB2SF_44100 = 2,
	HDMI_INFOFRAME_AUDIO_DB2SF_48000 = 3,
	HDMI_INFOFRAME_AUDIO_DB2SF_88200 = 4,
	HDMI_INFOFRAME_AUDIO_DB2SF_96000 = 5,
	HDMI_INFOFRAME_AUDIO_DB2SF_176400 = 6,
	HDMI_INFOFRAME_AUDIO_DB2SF_192000 = 7,
	HDMI_INFOFRAME_AUDIO_DB2SS_FROM_STREAM = 0,
	HDMI_INFOFRAME_AUDIO_DB2SS_16BIT = 1,
	HDMI_INFOFRAME_AUDIO_DB2SS_20BIT = 2,
	HDMI_INFOFRAME_AUDIO_DB2SS_24BIT = 3,
	HDMI_INFOFRAME_AUDIO_DB5_DM_INH_PERMITTED = 0,
	HDMI_INFOFRAME_AUDIO_DB5_DM_INH_PROHIBITED = 1
};

enum hdmi_packing_mode {
	HDMI_PACK_10b_RGB_YUV444 = 0,
	HDMI_PACK_24b_RGB_YUV444_YUV422 = 1,
	HDMI_PACK_20b_YUV422 = 2,
	HDMI_PACK_ALREADYPACKED = 7
};

enum hdmi_core_audio_sample_freq {
	HDMI_AUDIO_FS_32000 = 0x3,
	HDMI_AUDIO_FS_44100 = 0x0,
	HDMI_AUDIO_FS_48000 = 0x2,
	HDMI_AUDIO_FS_88200 = 0x8,
	HDMI_AUDIO_FS_96000 = 0xA,
	HDMI_AUDIO_FS_176400 = 0xC,
	HDMI_AUDIO_FS_192000 = 0xE,
	HDMI_AUDIO_FS_NOT_INDICATED = 0x1
};

enum hdmi_core_audio_layout {
	HDMI_AUDIO_LAYOUT_2CH = 0,
	HDMI_AUDIO_LAYOUT_8CH = 1
};

enum hdmi_core_cts_mode {
	HDMI_AUDIO_CTS_MODE_HW = 0,
	HDMI_AUDIO_CTS_MODE_SW = 1
};

enum hdmi_stereo_channels {
	HDMI_AUDIO_STEREO_NOCHANNELS = 0,
	HDMI_AUDIO_STEREO_ONECHANNEL = 1,
	HDMI_AUDIO_STEREO_TWOCHANNELS = 2,
	HDMI_AUDIO_STEREO_THREECHANNELS = 3,
	HDMI_AUDIO_STEREO_FOURCHANNELS = 4
};

enum hdmi_audio_type {
	HDMI_AUDIO_TYPE_LPCM = 0,
	HDMI_AUDIO_TYPE_IEC = 1
};

enum hdmi_audio_justify {
	HDMI_AUDIO_JUSTIFY_LEFT = 0,
	HDMI_AUDIO_JUSTIFY_RIGHT = 1
};

enum hdmi_audio_sample_order {
	HDMI_AUDIO_SAMPLE_RIGHT_FIRST = 0,
	HDMI_AUDIO_SAMPLE_LEFT_FIRST = 1
};

enum hdmi_audio_samples_perword {
	HDMI_AUDIO_ONEWORD_ONESAMPLE = 0,
	HDMI_AUDIO_ONEWORD_TWOSAMPLES = 1
};

enum hdmi_audio_sample_size {
	HDMI_AUDIO_SAMPLE_16BITS = 0,
	HDMI_AUDIO_SAMPLE_24BITS = 1
};

enum hdmi_audio_transf_mode {
	HDMI_AUDIO_TRANSF_DMA = 0,
	HDMI_AUDIO_TRANSF_IRQ = 1
};

enum hdmi_audio_blk_strt_end_sig {
	HDMI_AUDIO_BLOCK_SIG_STARTEND_ON = 0,
	HDMI_AUDIO_BLOCK_SIG_STARTEND_OFF = 1
};

enum hdmi_audio_i2s_config {
	HDMI_AUDIO_I2S_WS_POLARITY_LOW_IS_LEFT = 0,
	HDMI_AUDIO_I2S_WS_POLARIT_YLOW_IS_RIGHT = 1,
	HDMI_AUDIO_I2S_MSB_SHIFTED_FIRST = 0,
	HDMI_AUDIO_I2S_LSB_SHIFTED_FIRST = 1,
	HDMI_AUDIO_I2S_MAX_WORD_20BITS = 0,
	HDMI_AUDIO_I2S_MAX_WORD_24BITS = 1,
	HDMI_AUDIO_I2S_CHST_WORD_NOT_SPECIFIED = 0,
	HDMI_AUDIO_I2S_CHST_WORD_16_BITS = 1,
	HDMI_AUDIO_I2S_CHST_WORD_17_BITS = 6,
	HDMI_AUDIO_I2S_CHST_WORD_18_BITS = 2,
	HDMI_AUDIO_I2S_CHST_WORD_19_BITS = 4,
	HDMI_AUDIO_I2S_CHST_WORD_20_BITS_20MAX = 5,
	HDMI_AUDIO_I2S_CHST_WORD_20_BITS_24MAX = 1,
	HDMI_AUDIO_I2S_CHST_WORD_21_BITS = 6,
	HDMI_AUDIO_I2S_CHST_WORD_22_BITS = 2,
	HDMI_AUDIO_I2S_CHST_WORD_23_BITS = 4,
	HDMI_AUDIO_I2S_CHST_WORD_24_BITS = 5,
	HDMI_AUDIO_I2S_SCK_EDGE_FALLING = 0,
	HDMI_AUDIO_I2S_SCK_EDGE_RISING = 1,
	HDMI_AUDIO_I2S_VBIT_FOR_PCM = 0,
	HDMI_AUDIO_I2S_VBIT_FOR_COMPRESSED = 1,
	HDMI_AUDIO_I2S_INPUT_LENGTH_NA = 0,
	HDMI_AUDIO_I2S_INPUT_LENGTH_16 = 2,
	HDMI_AUDIO_I2S_INPUT_LENGTH_17 = 12,
	HDMI_AUDIO_I2S_INPUT_LENGTH_18 = 4,
	HDMI_AUDIO_I2S_INPUT_LENGTH_19 = 8,
	HDMI_AUDIO_I2S_INPUT_LENGTH_20 = 10,
	HDMI_AUDIO_I2S_INPUT_LENGTH_21 = 13,
	HDMI_AUDIO_I2S_INPUT_LENGTH_22 = 5,
	HDMI_AUDIO_I2S_INPUT_LENGTH_23 = 9,
	HDMI_AUDIO_I2S_INPUT_LENGTH_24 = 11,
	HDMI_AUDIO_I2S_FIRST_BIT_SHIFT = 0,
	HDMI_AUDIO_I2S_FIRST_BIT_NO_SHIFT = 1,
	HDMI_AUDIO_I2S_SD0_EN = 1,
	HDMI_AUDIO_I2S_SD1_EN = 1 << 1,
	HDMI_AUDIO_I2S_SD2_EN = 1 << 2,
	HDMI_AUDIO_I2S_SD3_EN = 1 << 3,
};

enum hdmi_audio_mclk_mode {
	HDMI_AUDIO_MCLK_128FS = 0,
	HDMI_AUDIO_MCLK_256FS = 1,
	HDMI_AUDIO_MCLK_384FS = 2,
	HDMI_AUDIO_MCLK_512FS = 3,
	HDMI_AUDIO_MCLK_768FS = 4,
	HDMI_AUDIO_MCLK_1024FS = 5,
	HDMI_AUDIO_MCLK_1152FS = 6,
	HDMI_AUDIO_MCLK_192FS = 7
};

struct hdmi_core_video_config {
	enum hdmi_core_inputbus_width	ip_bus_width;
	enum hdmi_core_dither_trunc	op_dither_truc;
	enum hdmi_core_deepcolor_ed	deep_color_pkt;
	enum hdmi_core_packet_mode	pkt_mode;
	enum hdmi_core_hdmi_dvi		hdmi_dvi;
	enum hdmi_core_tclkselclkmult	tclk_sel_clkmult;
};

/*
 * Refer to section 8.2 in HDMI 1.3 specification for
 * details about infoframe databytes
 */
struct hdmi_core_infoframe_audio {
	u8 db1_coding_type;
	u8 db1_channel_count;
	u8 db2_sample_freq;
	u8 db2_sample_size;
	u8 db4_channel_alloc;
	bool db5_downmix_inh;
	u8 db5_lsv;	/* Level shift values for downmix */
};

struct hdmi_core_packet_enable_repeat {
	u32	audio_pkt;
	u32	audio_pkt_repeat;
	u32	avi_infoframe;
	u32	avi_infoframe_repeat;
	u32	gen_cntrl_pkt;
	u32	gen_cntrl_pkt_repeat;
	u32	generic_pkt;
	u32	generic_pkt_repeat;
};

struct hdmi_video_format {
	enum hdmi_packing_mode	packing_mode;
	u32			y_res;	/* Line per panel */
	u32			x_res;	/* pixel per line */
};

struct hdmi_audio_format {
	enum hdmi_stereo_channels		stereo_channels;
	u8					active_chnnls_msk;
	enum hdmi_audio_type			type;
	enum hdmi_audio_justify			justification;
	enum hdmi_audio_sample_order		sample_order;
	enum hdmi_audio_samples_perword		samples_per_word;
	enum hdmi_audio_sample_size		sample_size;
	enum hdmi_audio_blk_strt_end_sig	en_sig_blk_strt_end;
};

struct hdmi_audio_dma {
	u8				transfer_size;
	u8				block_size;
	enum hdmi_audio_transf_mode	mode;
	u16				fifo_threshold;
};

struct hdmi_core_audio_i2s_config {
	u8 word_max_length;
	u8 word_length;
	u8 in_length_bits;
	u8 justification;
	u8 en_high_bitrate_aud;
	u8 sck_edge_mode;
	u8 cbit_order;
	u8 vbit;
	u8 ws_polarity;
	u8 direction;
	u8 shift;
	u8 active_sds;
};

struct hdmi_core_audio_config {
	struct hdmi_core_audio_i2s_config	i2s_cfg;
	enum hdmi_core_audio_sample_freq	freq_sample;
	bool					fs_override;
	u32					n;
	u32					cts;
	u32					aud_par_busclk;
	enum hdmi_core_audio_layout		layout;
	enum hdmi_core_cts_mode			cts_mode;
	bool					use_mclk;
	enum hdmi_audio_mclk_mode		mclk_mode;
	bool					en_acr_pkt;
	bool					en_dsd_audio;
	bool					en_parallel_aud_input;
	bool					en_spdif;
};

#if defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI) || \
	defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI_MODULE)
int hdmi_config_audio_acr(struct hdmi_ip_data *ip_data,
				u32 sample_freq, u32 *n, u32 *cts);
void hdmi_core_audio_infoframe_config(struct hdmi_ip_data *ip_data,
		struct hdmi_core_infoframe_audio *info_aud);
void hdmi_core_audio_config(struct hdmi_ip_data *ip_data,
					struct hdmi_core_audio_config *cfg);
void hdmi_wp_audio_config_dma(struct hdmi_ip_data *ip_data,
					struct hdmi_audio_dma *aud_dma);
void hdmi_wp_audio_config_format(struct hdmi_ip_data *ip_data,
					struct hdmi_audio_format *aud_fmt);
#endif
#endif
