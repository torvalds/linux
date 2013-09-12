/*
 * HDMI header definition for OMAP4 HDMI core IP
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

#ifndef _HDMI4_CORE_H_
#define _HDMI4_CORE_H_

#include "hdmi.h"

/* OMAP4 HDMI IP Core System */

#define HDMI_CORE_SYS_VND_IDL			0x0
#define HDMI_CORE_SYS_DEV_IDL			0x8
#define HDMI_CORE_SYS_DEV_IDH			0xC
#define HDMI_CORE_SYS_DEV_REV			0x10
#define HDMI_CORE_SYS_SRST			0x14
#define HDMI_CORE_SYS_SYS_CTRL1			0x20
#define HDMI_CORE_SYS_SYS_STAT			0x24
#define HDMI_CORE_SYS_SYS_CTRL3			0x28
#define HDMI_CORE_SYS_DCTL			0x34
#define HDMI_CORE_SYS_DE_DLY			0xC8
#define HDMI_CORE_SYS_DE_CTRL			0xCC
#define HDMI_CORE_SYS_DE_TOP			0xD0
#define HDMI_CORE_SYS_DE_CNTL			0xD8
#define HDMI_CORE_SYS_DE_CNTH			0xDC
#define HDMI_CORE_SYS_DE_LINL			0xE0
#define HDMI_CORE_SYS_DE_LINH_1			0xE4
#define HDMI_CORE_SYS_HRES_L			0xE8
#define HDMI_CORE_SYS_HRES_H			0xEC
#define HDMI_CORE_SYS_VRES_L			0xF0
#define HDMI_CORE_SYS_VRES_H			0xF4
#define HDMI_CORE_SYS_IADJUST			0xF8
#define HDMI_CORE_SYS_POLDETECT			0xFC
#define HDMI_CORE_SYS_HWIDTH1			0x110
#define HDMI_CORE_SYS_HWIDTH2			0x114
#define HDMI_CORE_SYS_VWIDTH			0x11C
#define HDMI_CORE_SYS_VID_CTRL			0x120
#define HDMI_CORE_SYS_VID_ACEN			0x124
#define HDMI_CORE_SYS_VID_MODE			0x128
#define HDMI_CORE_SYS_VID_BLANK1		0x12C
#define HDMI_CORE_SYS_VID_BLANK2		0x130
#define HDMI_CORE_SYS_VID_BLANK3		0x134
#define HDMI_CORE_SYS_DC_HEADER			0x138
#define HDMI_CORE_SYS_VID_DITHER		0x13C
#define HDMI_CORE_SYS_RGB2XVYCC_CT		0x140
#define HDMI_CORE_SYS_R2Y_COEFF_LOW		0x144
#define HDMI_CORE_SYS_R2Y_COEFF_UP		0x148
#define HDMI_CORE_SYS_G2Y_COEFF_LOW		0x14C
#define HDMI_CORE_SYS_G2Y_COEFF_UP		0x150
#define HDMI_CORE_SYS_B2Y_COEFF_LOW		0x154
#define HDMI_CORE_SYS_B2Y_COEFF_UP		0x158
#define HDMI_CORE_SYS_R2CB_COEFF_LOW		0x15C
#define HDMI_CORE_SYS_R2CB_COEFF_UP		0x160
#define HDMI_CORE_SYS_G2CB_COEFF_LOW		0x164
#define HDMI_CORE_SYS_G2CB_COEFF_UP		0x168
#define HDMI_CORE_SYS_B2CB_COEFF_LOW		0x16C
#define HDMI_CORE_SYS_B2CB_COEFF_UP		0x170
#define HDMI_CORE_SYS_R2CR_COEFF_LOW		0x174
#define HDMI_CORE_SYS_R2CR_COEFF_UP		0x178
#define HDMI_CORE_SYS_G2CR_COEFF_LOW		0x17C
#define HDMI_CORE_SYS_G2CR_COEFF_UP		0x180
#define HDMI_CORE_SYS_B2CR_COEFF_LOW		0x184
#define HDMI_CORE_SYS_B2CR_COEFF_UP		0x188
#define HDMI_CORE_SYS_RGB_OFFSET_LOW		0x18C
#define HDMI_CORE_SYS_RGB_OFFSET_UP		0x190
#define HDMI_CORE_SYS_Y_OFFSET_LOW		0x194
#define HDMI_CORE_SYS_Y_OFFSET_UP		0x198
#define HDMI_CORE_SYS_CBCR_OFFSET_LOW		0x19C
#define HDMI_CORE_SYS_CBCR_OFFSET_UP		0x1A0
#define HDMI_CORE_SYS_INTR_STATE		0x1C0
#define HDMI_CORE_SYS_INTR1			0x1C4
#define HDMI_CORE_SYS_INTR2			0x1C8
#define HDMI_CORE_SYS_INTR3			0x1CC
#define HDMI_CORE_SYS_INTR4			0x1D0
#define HDMI_CORE_SYS_INTR_UNMASK1		0x1D4
#define HDMI_CORE_SYS_INTR_UNMASK2		0x1D8
#define HDMI_CORE_SYS_INTR_UNMASK3		0x1DC
#define HDMI_CORE_SYS_INTR_UNMASK4		0x1E0
#define HDMI_CORE_SYS_INTR_CTRL			0x1E4
#define HDMI_CORE_SYS_TMDS_CTRL			0x208

/* value definitions for HDMI_CORE_SYS_SYS_CTRL1 fields */
#define HDMI_CORE_SYS_SYS_CTRL1_VEN_FOLLOWVSYNC	0x1
#define HDMI_CORE_SYS_SYS_CTRL1_HEN_FOLLOWHSYNC	0x1
#define HDMI_CORE_SYS_SYS_CTRL1_BSEL_24BITBUS	0x1
#define HDMI_CORE_SYS_SYS_CTRL1_EDGE_RISINGEDGE	0x1

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

enum hdmi_audio_i2s_config {
	HDMI_AUDIO_I2S_MSB_SHIFTED_FIRST = 0,
	HDMI_AUDIO_I2S_LSB_SHIFTED_FIRST = 1,
	HDMI_AUDIO_I2S_SCK_EDGE_FALLING = 0,
	HDMI_AUDIO_I2S_SCK_EDGE_RISING = 1,
	HDMI_AUDIO_I2S_VBIT_FOR_PCM = 0,
	HDMI_AUDIO_I2S_VBIT_FOR_COMPRESSED = 1,
	HDMI_AUDIO_I2S_FIRST_BIT_SHIFT = 0,
	HDMI_AUDIO_I2S_FIRST_BIT_NO_SHIFT = 1,
	HDMI_AUDIO_I2S_SD0_EN = 1,
	HDMI_AUDIO_I2S_SD1_EN = 1 << 1,
	HDMI_AUDIO_I2S_SD2_EN = 1 << 2,
	HDMI_AUDIO_I2S_SD3_EN = 1 << 3,
};

struct hdmi_core_video_config {
	enum hdmi_core_inputbus_width	ip_bus_width;
	enum hdmi_core_dither_trunc	op_dither_truc;
	enum hdmi_core_deepcolor_ed	deep_color_pkt;
	enum hdmi_core_packet_mode	pkt_mode;
	enum hdmi_core_hdmi_dvi		hdmi_dvi;
	enum hdmi_core_tclkselclkmult	tclk_sel_clkmult;
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

int hdmi4_read_edid(struct hdmi_core_data *core, u8 *edid, int len);
void hdmi4_configure(struct hdmi_core_data *core, struct hdmi_wp_data *wp,
		struct hdmi_config *cfg);
void hdmi4_core_dump(struct hdmi_core_data *core, struct seq_file *s);
int hdmi4_core_init(struct platform_device *pdev, struct hdmi_core_data *core);

#if defined(CONFIG_OMAP4_DSS_HDMI_AUDIO)
int hdmi4_audio_start(struct hdmi_core_data *core, struct hdmi_wp_data *wp);
void hdmi4_audio_stop(struct hdmi_core_data *core, struct hdmi_wp_data *wp);
int hdmi4_audio_config(struct hdmi_core_data *core, struct hdmi_wp_data *wp,
		struct omap_dss_audio *audio);
int hdmi4_audio_get_dma_port(u32 *offset, u32 *size);
#endif

#endif
