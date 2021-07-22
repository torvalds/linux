// SPDX-License-Identifier: GPL-2.0-only
/*
 * ti_hdmi_4xxx_ip.c
 *
 * HDMI TI81xx, TI38xx, TI OMAP4 etc IP driver Library
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - https://www.ti.com/
 * Authors: Yong Zhi
 *	Mythri pk <mythripk@ti.com>
 */

#define DSS_SUBSYS_NAME "HDMICORE"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include <sound/asound.h>
#include <sound/asoundef.h>

#include "hdmi4_core.h"
#include "dss_features.h"

#define HDMI_CORE_AV		0x500

static inline void __iomem *hdmi_av_base(struct hdmi_core_data *core)
{
	return core->base + HDMI_CORE_AV;
}

static int hdmi_core_ddc_init(struct hdmi_core_data *core)
{
	void __iomem *base = core->base;

	/* Turn on CLK for DDC */
	REG_FLD_MOD(base, HDMI_CORE_AV_DPD, 0x7, 2, 0);

	/* IN_PROG */
	if (REG_GET(base, HDMI_CORE_DDC_STATUS, 4, 4) == 1) {
		/* Abort transaction */
		REG_FLD_MOD(base, HDMI_CORE_DDC_CMD, 0xf, 3, 0);
		/* IN_PROG */
		if (hdmi_wait_for_bit_change(base, HDMI_CORE_DDC_STATUS,
					4, 4, 0) != 0) {
			DSSERR("Timeout aborting DDC transaction\n");
			return -ETIMEDOUT;
		}
	}

	/* Clk SCL Devices */
	REG_FLD_MOD(base, HDMI_CORE_DDC_CMD, 0xA, 3, 0);

	/* HDMI_CORE_DDC_STATUS_IN_PROG */
	if (hdmi_wait_for_bit_change(base, HDMI_CORE_DDC_STATUS,
				4, 4, 0) != 0) {
		DSSERR("Timeout starting SCL clock\n");
		return -ETIMEDOUT;
	}

	/* Clear FIFO */
	REG_FLD_MOD(base, HDMI_CORE_DDC_CMD, 0x9, 3, 0);

	/* HDMI_CORE_DDC_STATUS_IN_PROG */
	if (hdmi_wait_for_bit_change(base, HDMI_CORE_DDC_STATUS,
				4, 4, 0) != 0) {
		DSSERR("Timeout clearing DDC fifo\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int hdmi_core_ddc_edid(struct hdmi_core_data *core,
		u8 *pedid, int ext)
{
	void __iomem *base = core->base;
	u32 i;
	char checksum;
	u32 offset = 0;

	/* HDMI_CORE_DDC_STATUS_IN_PROG */
	if (hdmi_wait_for_bit_change(base, HDMI_CORE_DDC_STATUS,
				4, 4, 0) != 0) {
		DSSERR("Timeout waiting DDC to be ready\n");
		return -ETIMEDOUT;
	}

	if (ext % 2 != 0)
		offset = 0x80;

	/* Load Segment Address Register */
	REG_FLD_MOD(base, HDMI_CORE_DDC_SEGM, ext / 2, 7, 0);

	/* Load Slave Address Register */
	REG_FLD_MOD(base, HDMI_CORE_DDC_ADDR, 0xA0 >> 1, 7, 1);

	/* Load Offset Address Register */
	REG_FLD_MOD(base, HDMI_CORE_DDC_OFFSET, offset, 7, 0);

	/* Load Byte Count */
	REG_FLD_MOD(base, HDMI_CORE_DDC_COUNT1, 0x80, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_DDC_COUNT2, 0x0, 1, 0);

	/* Set DDC_CMD */
	if (ext)
		REG_FLD_MOD(base, HDMI_CORE_DDC_CMD, 0x4, 3, 0);
	else
		REG_FLD_MOD(base, HDMI_CORE_DDC_CMD, 0x2, 3, 0);

	/* HDMI_CORE_DDC_STATUS_BUS_LOW */
	if (REG_GET(base, HDMI_CORE_DDC_STATUS, 6, 6) == 1) {
		DSSERR("I2C Bus Low?\n");
		return -EIO;
	}
	/* HDMI_CORE_DDC_STATUS_NO_ACK */
	if (REG_GET(base, HDMI_CORE_DDC_STATUS, 5, 5) == 1) {
		DSSERR("I2C No Ack\n");
		return -EIO;
	}

	for (i = 0; i < 0x80; ++i) {
		int t;

		/* IN_PROG */
		if (REG_GET(base, HDMI_CORE_DDC_STATUS, 4, 4) == 0) {
			DSSERR("operation stopped when reading edid\n");
			return -EIO;
		}

		t = 0;
		/* FIFO_EMPTY */
		while (REG_GET(base, HDMI_CORE_DDC_STATUS, 2, 2) == 1) {
			if (t++ > 10000) {
				DSSERR("timeout reading edid\n");
				return -ETIMEDOUT;
			}
			udelay(1);
		}

		pedid[i] = REG_GET(base, HDMI_CORE_DDC_DATA, 7, 0);
	}

	checksum = 0;
	for (i = 0; i < 0x80; ++i)
		checksum += pedid[i];

	if (checksum != 0) {
		DSSERR("E-EDID checksum failed!!\n");
		return -EIO;
	}

	return 0;
}

int hdmi4_read_edid(struct hdmi_core_data *core, u8 *edid, int len)
{
	int r, l;

	if (len < 128)
		return -EINVAL;

	r = hdmi_core_ddc_init(core);
	if (r)
		return r;

	r = hdmi_core_ddc_edid(core, edid, 0);
	if (r)
		return r;

	l = 128;

	if (len >= 128 * 2 && edid[0x7e] > 0) {
		r = hdmi_core_ddc_edid(core, edid + 0x80, 1);
		if (r)
			return r;
		l += 128;
	}

	return l;
}

static void hdmi_core_init(struct hdmi_core_video_config *video_cfg)
{
	DSSDBG("Enter hdmi_core_init\n");

	/* video core */
	video_cfg->ip_bus_width = HDMI_INPUT_8BIT;
	video_cfg->op_dither_truc = HDMI_OUTPUTTRUNCATION_8BIT;
	video_cfg->deep_color_pkt = HDMI_DEEPCOLORPACKECTDISABLE;
	video_cfg->pkt_mode = HDMI_PACKETMODERESERVEDVALUE;
	video_cfg->hdmi_dvi = HDMI_DVI;
	video_cfg->tclk_sel_clkmult = HDMI_FPLL10IDCK;
}

static void hdmi_core_powerdown_disable(struct hdmi_core_data *core)
{
	DSSDBG("Enter hdmi_core_powerdown_disable\n");
	REG_FLD_MOD(core->base, HDMI_CORE_SYS_SYS_CTRL1, 0x0, 0, 0);
}

static void hdmi_core_swreset_release(struct hdmi_core_data *core)
{
	DSSDBG("Enter hdmi_core_swreset_release\n");
	REG_FLD_MOD(core->base, HDMI_CORE_SYS_SRST, 0x0, 0, 0);
}

static void hdmi_core_swreset_assert(struct hdmi_core_data *core)
{
	DSSDBG("Enter hdmi_core_swreset_assert\n");
	REG_FLD_MOD(core->base, HDMI_CORE_SYS_SRST, 0x1, 0, 0);
}

/* HDMI_CORE_VIDEO_CONFIG */
static void hdmi_core_video_config(struct hdmi_core_data *core,
				struct hdmi_core_video_config *cfg)
{
	u32 r = 0;
	void __iomem *core_sys_base = core->base;
	void __iomem *core_av_base = hdmi_av_base(core);

	/* sys_ctrl1 default configuration not tunable */
	r = hdmi_read_reg(core_sys_base, HDMI_CORE_SYS_SYS_CTRL1);
	r = FLD_MOD(r, HDMI_CORE_SYS_SYS_CTRL1_VEN_FOLLOWVSYNC, 5, 5);
	r = FLD_MOD(r, HDMI_CORE_SYS_SYS_CTRL1_HEN_FOLLOWHSYNC, 4, 4);
	r = FLD_MOD(r, HDMI_CORE_SYS_SYS_CTRL1_BSEL_24BITBUS, 2, 2);
	r = FLD_MOD(r, HDMI_CORE_SYS_SYS_CTRL1_EDGE_RISINGEDGE, 1, 1);
	hdmi_write_reg(core_sys_base, HDMI_CORE_SYS_SYS_CTRL1, r);

	REG_FLD_MOD(core_sys_base,
			HDMI_CORE_SYS_VID_ACEN, cfg->ip_bus_width, 7, 6);

	/* Vid_Mode */
	r = hdmi_read_reg(core_sys_base, HDMI_CORE_SYS_VID_MODE);

	/* dither truncation configuration */
	if (cfg->op_dither_truc > HDMI_OUTPUTTRUNCATION_12BIT) {
		r = FLD_MOD(r, cfg->op_dither_truc - 3, 7, 6);
		r = FLD_MOD(r, 1, 5, 5);
	} else {
		r = FLD_MOD(r, cfg->op_dither_truc, 7, 6);
		r = FLD_MOD(r, 0, 5, 5);
	}
	hdmi_write_reg(core_sys_base, HDMI_CORE_SYS_VID_MODE, r);

	/* HDMI_Ctrl */
	r = hdmi_read_reg(core_av_base, HDMI_CORE_AV_HDMI_CTRL);
	r = FLD_MOD(r, cfg->deep_color_pkt, 6, 6);
	r = FLD_MOD(r, cfg->pkt_mode, 5, 3);
	r = FLD_MOD(r, cfg->hdmi_dvi, 0, 0);
	hdmi_write_reg(core_av_base, HDMI_CORE_AV_HDMI_CTRL, r);

	/* TMDS_CTRL */
	REG_FLD_MOD(core_sys_base,
			HDMI_CORE_SYS_TMDS_CTRL, cfg->tclk_sel_clkmult, 6, 5);
}

static void hdmi_core_write_avi_infoframe(struct hdmi_core_data *core,
	struct hdmi_avi_infoframe *frame)
{
	void __iomem *av_base = hdmi_av_base(core);
	u8 data[HDMI_INFOFRAME_SIZE(AVI)];
	int i;

	hdmi_avi_infoframe_pack(frame, data, sizeof(data));

	print_hex_dump_debug("AVI: ", DUMP_PREFIX_NONE, 16, 1, data,
		HDMI_INFOFRAME_SIZE(AVI), false);

	for (i = 0; i < sizeof(data); ++i) {
		hdmi_write_reg(av_base, HDMI_CORE_AV_AVI_BASE + i * 4,
			data[i]);
	}
}

static void hdmi_core_av_packet_config(struct hdmi_core_data *core,
		struct hdmi_core_packet_enable_repeat repeat_cfg)
{
	/* enable/repeat the infoframe */
	hdmi_write_reg(hdmi_av_base(core), HDMI_CORE_AV_PB_CTRL1,
		(repeat_cfg.audio_pkt << 5) |
		(repeat_cfg.audio_pkt_repeat << 4) |
		(repeat_cfg.avi_infoframe << 1) |
		(repeat_cfg.avi_infoframe_repeat));

	/* enable/repeat the packet */
	hdmi_write_reg(hdmi_av_base(core), HDMI_CORE_AV_PB_CTRL2,
		(repeat_cfg.gen_cntrl_pkt << 3) |
		(repeat_cfg.gen_cntrl_pkt_repeat << 2) |
		(repeat_cfg.generic_pkt << 1) |
		(repeat_cfg.generic_pkt_repeat));
}

void hdmi4_configure(struct hdmi_core_data *core,
	struct hdmi_wp_data *wp, struct hdmi_config *cfg)
{
	/* HDMI */
	struct omap_video_timings video_timing;
	struct hdmi_video_format video_format;
	/* HDMI core */
	struct hdmi_core_video_config v_core_cfg;
	struct hdmi_core_packet_enable_repeat repeat_cfg = { 0 };

	hdmi_core_init(&v_core_cfg);

	hdmi_wp_init_vid_fmt_timings(&video_format, &video_timing, cfg);

	hdmi_wp_video_config_timing(wp, &video_timing);

	/* video config */
	video_format.packing_mode = HDMI_PACK_24b_RGB_YUV444_YUV422;

	hdmi_wp_video_config_format(wp, &video_format);

	hdmi_wp_video_config_interface(wp, &video_timing);

	/*
	 * configure core video part
	 * set software reset in the core
	 */
	hdmi_core_swreset_assert(core);

	/* power down off */
	hdmi_core_powerdown_disable(core);

	v_core_cfg.pkt_mode = HDMI_PACKETMODE24BITPERPIXEL;
	v_core_cfg.hdmi_dvi = cfg->hdmi_dvi_mode;

	hdmi_core_video_config(core, &v_core_cfg);

	/* release software reset in the core */
	hdmi_core_swreset_release(core);

	if (cfg->hdmi_dvi_mode == HDMI_HDMI) {
		hdmi_core_write_avi_infoframe(core, &cfg->infoframe);

		/* enable/repeat the infoframe */
		repeat_cfg.avi_infoframe = HDMI_PACKETENABLE;
		repeat_cfg.avi_infoframe_repeat = HDMI_PACKETREPEATON;
		/* wakeup */
		repeat_cfg.audio_pkt = HDMI_PACKETENABLE;
		repeat_cfg.audio_pkt_repeat = HDMI_PACKETREPEATON;
	}

	hdmi_core_av_packet_config(core, repeat_cfg);
}

void hdmi4_core_dump(struct hdmi_core_data *core, struct seq_file *s)
{
	int i;

#define CORE_REG(i, name) name(i)
#define DUMPCORE(r) seq_printf(s, "%-35s %08x\n", #r,\
		hdmi_read_reg(core->base, r))
#define DUMPCOREAV(r) seq_printf(s, "%-35s %08x\n", #r,\
		hdmi_read_reg(hdmi_av_base(core), r))
#define DUMPCOREAV2(i, r) seq_printf(s, "%s[%d]%*s %08x\n", #r, i, \
		(i < 10) ? 32 - (int)strlen(#r) : 31 - (int)strlen(#r), " ", \
		hdmi_read_reg(hdmi_av_base(core), CORE_REG(i, r)))

	DUMPCORE(HDMI_CORE_SYS_VND_IDL);
	DUMPCORE(HDMI_CORE_SYS_DEV_IDL);
	DUMPCORE(HDMI_CORE_SYS_DEV_IDH);
	DUMPCORE(HDMI_CORE_SYS_DEV_REV);
	DUMPCORE(HDMI_CORE_SYS_SRST);
	DUMPCORE(HDMI_CORE_SYS_SYS_CTRL1);
	DUMPCORE(HDMI_CORE_SYS_SYS_STAT);
	DUMPCORE(HDMI_CORE_SYS_SYS_CTRL3);
	DUMPCORE(HDMI_CORE_SYS_DE_DLY);
	DUMPCORE(HDMI_CORE_SYS_DE_CTRL);
	DUMPCORE(HDMI_CORE_SYS_DE_TOP);
	DUMPCORE(HDMI_CORE_SYS_DE_CNTL);
	DUMPCORE(HDMI_CORE_SYS_DE_CNTH);
	DUMPCORE(HDMI_CORE_SYS_DE_LINL);
	DUMPCORE(HDMI_CORE_SYS_DE_LINH_1);
	DUMPCORE(HDMI_CORE_SYS_HRES_L);
	DUMPCORE(HDMI_CORE_SYS_HRES_H);
	DUMPCORE(HDMI_CORE_SYS_VRES_L);
	DUMPCORE(HDMI_CORE_SYS_VRES_H);
	DUMPCORE(HDMI_CORE_SYS_IADJUST);
	DUMPCORE(HDMI_CORE_SYS_POLDETECT);
	DUMPCORE(HDMI_CORE_SYS_HWIDTH1);
	DUMPCORE(HDMI_CORE_SYS_HWIDTH2);
	DUMPCORE(HDMI_CORE_SYS_VWIDTH);
	DUMPCORE(HDMI_CORE_SYS_VID_CTRL);
	DUMPCORE(HDMI_CORE_SYS_VID_ACEN);
	DUMPCORE(HDMI_CORE_SYS_VID_MODE);
	DUMPCORE(HDMI_CORE_SYS_VID_BLANK1);
	DUMPCORE(HDMI_CORE_SYS_VID_BLANK3);
	DUMPCORE(HDMI_CORE_SYS_VID_BLANK1);
	DUMPCORE(HDMI_CORE_SYS_DC_HEADER);
	DUMPCORE(HDMI_CORE_SYS_VID_DITHER);
	DUMPCORE(HDMI_CORE_SYS_RGB2XVYCC_CT);
	DUMPCORE(HDMI_CORE_SYS_R2Y_COEFF_LOW);
	DUMPCORE(HDMI_CORE_SYS_R2Y_COEFF_UP);
	DUMPCORE(HDMI_CORE_SYS_G2Y_COEFF_LOW);
	DUMPCORE(HDMI_CORE_SYS_G2Y_COEFF_UP);
	DUMPCORE(HDMI_CORE_SYS_B2Y_COEFF_LOW);
	DUMPCORE(HDMI_CORE_SYS_B2Y_COEFF_UP);
	DUMPCORE(HDMI_CORE_SYS_R2CB_COEFF_LOW);
	DUMPCORE(HDMI_CORE_SYS_R2CB_COEFF_UP);
	DUMPCORE(HDMI_CORE_SYS_G2CB_COEFF_LOW);
	DUMPCORE(HDMI_CORE_SYS_G2CB_COEFF_UP);
	DUMPCORE(HDMI_CORE_SYS_B2CB_COEFF_LOW);
	DUMPCORE(HDMI_CORE_SYS_B2CB_COEFF_UP);
	DUMPCORE(HDMI_CORE_SYS_R2CR_COEFF_LOW);
	DUMPCORE(HDMI_CORE_SYS_R2CR_COEFF_UP);
	DUMPCORE(HDMI_CORE_SYS_G2CR_COEFF_LOW);
	DUMPCORE(HDMI_CORE_SYS_G2CR_COEFF_UP);
	DUMPCORE(HDMI_CORE_SYS_B2CR_COEFF_LOW);
	DUMPCORE(HDMI_CORE_SYS_B2CR_COEFF_UP);
	DUMPCORE(HDMI_CORE_SYS_RGB_OFFSET_LOW);
	DUMPCORE(HDMI_CORE_SYS_RGB_OFFSET_UP);
	DUMPCORE(HDMI_CORE_SYS_Y_OFFSET_LOW);
	DUMPCORE(HDMI_CORE_SYS_Y_OFFSET_UP);
	DUMPCORE(HDMI_CORE_SYS_CBCR_OFFSET_LOW);
	DUMPCORE(HDMI_CORE_SYS_CBCR_OFFSET_UP);
	DUMPCORE(HDMI_CORE_SYS_INTR_STATE);
	DUMPCORE(HDMI_CORE_SYS_INTR1);
	DUMPCORE(HDMI_CORE_SYS_INTR2);
	DUMPCORE(HDMI_CORE_SYS_INTR3);
	DUMPCORE(HDMI_CORE_SYS_INTR4);
	DUMPCORE(HDMI_CORE_SYS_INTR_UNMASK1);
	DUMPCORE(HDMI_CORE_SYS_INTR_UNMASK2);
	DUMPCORE(HDMI_CORE_SYS_INTR_UNMASK3);
	DUMPCORE(HDMI_CORE_SYS_INTR_UNMASK4);
	DUMPCORE(HDMI_CORE_SYS_INTR_CTRL);
	DUMPCORE(HDMI_CORE_SYS_TMDS_CTRL);

	DUMPCORE(HDMI_CORE_DDC_ADDR);
	DUMPCORE(HDMI_CORE_DDC_SEGM);
	DUMPCORE(HDMI_CORE_DDC_OFFSET);
	DUMPCORE(HDMI_CORE_DDC_COUNT1);
	DUMPCORE(HDMI_CORE_DDC_COUNT2);
	DUMPCORE(HDMI_CORE_DDC_STATUS);
	DUMPCORE(HDMI_CORE_DDC_CMD);
	DUMPCORE(HDMI_CORE_DDC_DATA);

	DUMPCOREAV(HDMI_CORE_AV_ACR_CTRL);
	DUMPCOREAV(HDMI_CORE_AV_FREQ_SVAL);
	DUMPCOREAV(HDMI_CORE_AV_N_SVAL1);
	DUMPCOREAV(HDMI_CORE_AV_N_SVAL2);
	DUMPCOREAV(HDMI_CORE_AV_N_SVAL3);
	DUMPCOREAV(HDMI_CORE_AV_CTS_SVAL1);
	DUMPCOREAV(HDMI_CORE_AV_CTS_SVAL2);
	DUMPCOREAV(HDMI_CORE_AV_CTS_SVAL3);
	DUMPCOREAV(HDMI_CORE_AV_CTS_HVAL1);
	DUMPCOREAV(HDMI_CORE_AV_CTS_HVAL2);
	DUMPCOREAV(HDMI_CORE_AV_CTS_HVAL3);
	DUMPCOREAV(HDMI_CORE_AV_AUD_MODE);
	DUMPCOREAV(HDMI_CORE_AV_SPDIF_CTRL);
	DUMPCOREAV(HDMI_CORE_AV_HW_SPDIF_FS);
	DUMPCOREAV(HDMI_CORE_AV_SWAP_I2S);
	DUMPCOREAV(HDMI_CORE_AV_SPDIF_ERTH);
	DUMPCOREAV(HDMI_CORE_AV_I2S_IN_MAP);
	DUMPCOREAV(HDMI_CORE_AV_I2S_IN_CTRL);
	DUMPCOREAV(HDMI_CORE_AV_I2S_CHST0);
	DUMPCOREAV(HDMI_CORE_AV_I2S_CHST1);
	DUMPCOREAV(HDMI_CORE_AV_I2S_CHST2);
	DUMPCOREAV(HDMI_CORE_AV_I2S_CHST4);
	DUMPCOREAV(HDMI_CORE_AV_I2S_CHST5);
	DUMPCOREAV(HDMI_CORE_AV_ASRC);
	DUMPCOREAV(HDMI_CORE_AV_I2S_IN_LEN);
	DUMPCOREAV(HDMI_CORE_AV_HDMI_CTRL);
	DUMPCOREAV(HDMI_CORE_AV_AUDO_TXSTAT);
	DUMPCOREAV(HDMI_CORE_AV_AUD_PAR_BUSCLK_1);
	DUMPCOREAV(HDMI_CORE_AV_AUD_PAR_BUSCLK_2);
	DUMPCOREAV(HDMI_CORE_AV_AUD_PAR_BUSCLK_3);
	DUMPCOREAV(HDMI_CORE_AV_TEST_TXCTRL);
	DUMPCOREAV(HDMI_CORE_AV_DPD);
	DUMPCOREAV(HDMI_CORE_AV_PB_CTRL1);
	DUMPCOREAV(HDMI_CORE_AV_PB_CTRL2);
	DUMPCOREAV(HDMI_CORE_AV_AVI_TYPE);
	DUMPCOREAV(HDMI_CORE_AV_AVI_VERS);
	DUMPCOREAV(HDMI_CORE_AV_AVI_LEN);
	DUMPCOREAV(HDMI_CORE_AV_AVI_CHSUM);

	for (i = 0; i < HDMI_CORE_AV_AVI_DBYTE_NELEMS; i++)
		DUMPCOREAV2(i, HDMI_CORE_AV_AVI_DBYTE);

	DUMPCOREAV(HDMI_CORE_AV_SPD_TYPE);
	DUMPCOREAV(HDMI_CORE_AV_SPD_VERS);
	DUMPCOREAV(HDMI_CORE_AV_SPD_LEN);
	DUMPCOREAV(HDMI_CORE_AV_SPD_CHSUM);

	for (i = 0; i < HDMI_CORE_AV_SPD_DBYTE_NELEMS; i++)
		DUMPCOREAV2(i, HDMI_CORE_AV_SPD_DBYTE);

	DUMPCOREAV(HDMI_CORE_AV_AUDIO_TYPE);
	DUMPCOREAV(HDMI_CORE_AV_AUDIO_VERS);
	DUMPCOREAV(HDMI_CORE_AV_AUDIO_LEN);
	DUMPCOREAV(HDMI_CORE_AV_AUDIO_CHSUM);

	for (i = 0; i < HDMI_CORE_AV_AUD_DBYTE_NELEMS; i++)
		DUMPCOREAV2(i, HDMI_CORE_AV_AUD_DBYTE);

	DUMPCOREAV(HDMI_CORE_AV_MPEG_TYPE);
	DUMPCOREAV(HDMI_CORE_AV_MPEG_VERS);
	DUMPCOREAV(HDMI_CORE_AV_MPEG_LEN);
	DUMPCOREAV(HDMI_CORE_AV_MPEG_CHSUM);

	for (i = 0; i < HDMI_CORE_AV_MPEG_DBYTE_NELEMS; i++)
		DUMPCOREAV2(i, HDMI_CORE_AV_MPEG_DBYTE);

	for (i = 0; i < HDMI_CORE_AV_GEN_DBYTE_NELEMS; i++)
		DUMPCOREAV2(i, HDMI_CORE_AV_GEN_DBYTE);

	DUMPCOREAV(HDMI_CORE_AV_CP_BYTE1);

	for (i = 0; i < HDMI_CORE_AV_GEN2_DBYTE_NELEMS; i++)
		DUMPCOREAV2(i, HDMI_CORE_AV_GEN2_DBYTE);

	DUMPCOREAV(HDMI_CORE_AV_CEC_ADDR_ID);
}

static void hdmi_core_audio_config(struct hdmi_core_data *core,
					struct hdmi_core_audio_config *cfg)
{
	u32 r;
	void __iomem *av_base = hdmi_av_base(core);

	/*
	 * Parameters for generation of Audio Clock Recovery packets
	 */
	REG_FLD_MOD(av_base, HDMI_CORE_AV_N_SVAL1, cfg->n, 7, 0);
	REG_FLD_MOD(av_base, HDMI_CORE_AV_N_SVAL2, cfg->n >> 8, 7, 0);
	REG_FLD_MOD(av_base, HDMI_CORE_AV_N_SVAL3, cfg->n >> 16, 7, 0);

	if (cfg->cts_mode == HDMI_AUDIO_CTS_MODE_SW) {
		REG_FLD_MOD(av_base, HDMI_CORE_AV_CTS_SVAL1, cfg->cts, 7, 0);
		REG_FLD_MOD(av_base,
				HDMI_CORE_AV_CTS_SVAL2, cfg->cts >> 8, 7, 0);
		REG_FLD_MOD(av_base,
				HDMI_CORE_AV_CTS_SVAL3, cfg->cts >> 16, 7, 0);
	} else {
		REG_FLD_MOD(av_base, HDMI_CORE_AV_AUD_PAR_BUSCLK_1,
				cfg->aud_par_busclk, 7, 0);
		REG_FLD_MOD(av_base, HDMI_CORE_AV_AUD_PAR_BUSCLK_2,
				(cfg->aud_par_busclk >> 8), 7, 0);
		REG_FLD_MOD(av_base, HDMI_CORE_AV_AUD_PAR_BUSCLK_3,
				(cfg->aud_par_busclk >> 16), 7, 0);
	}

	/* Set ACR clock divisor */
	REG_FLD_MOD(av_base,
			HDMI_CORE_AV_FREQ_SVAL, cfg->mclk_mode, 2, 0);

	r = hdmi_read_reg(av_base, HDMI_CORE_AV_ACR_CTRL);
	/*
	 * Use TMDS clock for ACR packets. For devices that use
	 * the MCLK, this is the first part of the MCLK initialization.
	 */
	r = FLD_MOD(r, 0, 2, 2);

	r = FLD_MOD(r, cfg->en_acr_pkt, 1, 1);
	r = FLD_MOD(r, cfg->cts_mode, 0, 0);
	hdmi_write_reg(av_base, HDMI_CORE_AV_ACR_CTRL, r);

	/* For devices using MCLK, this completes its initialization. */
	if (cfg->use_mclk)
		REG_FLD_MOD(av_base, HDMI_CORE_AV_ACR_CTRL, 1, 2, 2);

	/* Override of SPDIF sample frequency with value in I2S_CHST4 */
	REG_FLD_MOD(av_base, HDMI_CORE_AV_SPDIF_CTRL,
						cfg->fs_override, 1, 1);

	/*
	 * Set IEC-60958-3 channel status word. It is passed to the IP
	 * just as it is received. The user of the driver is responsible
	 * for its contents.
	 */
	hdmi_write_reg(av_base, HDMI_CORE_AV_I2S_CHST0,
		       cfg->iec60958_cfg->status[0]);
	hdmi_write_reg(av_base, HDMI_CORE_AV_I2S_CHST1,
		       cfg->iec60958_cfg->status[1]);
	hdmi_write_reg(av_base, HDMI_CORE_AV_I2S_CHST2,
		       cfg->iec60958_cfg->status[2]);
	/* yes, this is correct: status[3] goes to CHST4 register */
	hdmi_write_reg(av_base, HDMI_CORE_AV_I2S_CHST4,
		       cfg->iec60958_cfg->status[3]);
	/* yes, this is correct: status[4] goes to CHST5 register */
	hdmi_write_reg(av_base, HDMI_CORE_AV_I2S_CHST5,
		       cfg->iec60958_cfg->status[4]);

	/* set I2S parameters */
	r = hdmi_read_reg(av_base, HDMI_CORE_AV_I2S_IN_CTRL);
	r = FLD_MOD(r, cfg->i2s_cfg.sck_edge_mode, 6, 6);
	r = FLD_MOD(r, cfg->i2s_cfg.vbit, 4, 4);
	r = FLD_MOD(r, cfg->i2s_cfg.justification, 2, 2);
	r = FLD_MOD(r, cfg->i2s_cfg.direction, 1, 1);
	r = FLD_MOD(r, cfg->i2s_cfg.shift, 0, 0);
	hdmi_write_reg(av_base, HDMI_CORE_AV_I2S_IN_CTRL, r);

	REG_FLD_MOD(av_base, HDMI_CORE_AV_I2S_IN_LEN,
			cfg->i2s_cfg.in_length_bits, 3, 0);

	/* Audio channels and mode parameters */
	REG_FLD_MOD(av_base, HDMI_CORE_AV_HDMI_CTRL, cfg->layout, 2, 1);
	r = hdmi_read_reg(av_base, HDMI_CORE_AV_AUD_MODE);
	r = FLD_MOD(r, cfg->i2s_cfg.active_sds, 7, 4);
	r = FLD_MOD(r, cfg->en_dsd_audio, 3, 3);
	r = FLD_MOD(r, cfg->en_parallel_aud_input, 2, 2);
	r = FLD_MOD(r, cfg->en_spdif, 1, 1);
	hdmi_write_reg(av_base, HDMI_CORE_AV_AUD_MODE, r);

	/* Audio channel mappings */
	/* TODO: Make channel mapping dynamic. For now, map channels
	 * in the ALSA order: FL/FR/RL/RR/C/LFE/SL/SR. Remapping is needed as
	 * HDMI speaker order is different. See CEA-861 Section 6.6.2.
	 */
	hdmi_write_reg(av_base, HDMI_CORE_AV_I2S_IN_MAP, 0x78);
	REG_FLD_MOD(av_base, HDMI_CORE_AV_SWAP_I2S, 1, 5, 5);
}

static void hdmi_core_audio_infoframe_cfg(struct hdmi_core_data *core,
		struct snd_cea_861_aud_if *info_aud)
{
	u8 sum = 0, checksum = 0;
	void __iomem *av_base = hdmi_av_base(core);

	/*
	 * Set audio info frame type, version and length as
	 * described in HDMI 1.4a Section 8.2.2 specification.
	 * Checksum calculation is defined in Section 5.3.5.
	 */
	hdmi_write_reg(av_base, HDMI_CORE_AV_AUDIO_TYPE, 0x84);
	hdmi_write_reg(av_base, HDMI_CORE_AV_AUDIO_VERS, 0x01);
	hdmi_write_reg(av_base, HDMI_CORE_AV_AUDIO_LEN, 0x0a);
	sum += 0x84 + 0x001 + 0x00a;

	hdmi_write_reg(av_base, HDMI_CORE_AV_AUD_DBYTE(0),
		       info_aud->db1_ct_cc);
	sum += info_aud->db1_ct_cc;

	hdmi_write_reg(av_base, HDMI_CORE_AV_AUD_DBYTE(1),
		       info_aud->db2_sf_ss);
	sum += info_aud->db2_sf_ss;

	hdmi_write_reg(av_base, HDMI_CORE_AV_AUD_DBYTE(2), info_aud->db3);
	sum += info_aud->db3;

	/*
	 * The OMAP HDMI IP requires to use the 8-channel channel code when
	 * transmitting more than two channels.
	 */
	if (info_aud->db4_ca != 0x00)
		info_aud->db4_ca = 0x13;

	hdmi_write_reg(av_base, HDMI_CORE_AV_AUD_DBYTE(3), info_aud->db4_ca);
	sum += info_aud->db4_ca;

	hdmi_write_reg(av_base, HDMI_CORE_AV_AUD_DBYTE(4),
		       info_aud->db5_dminh_lsv);
	sum += info_aud->db5_dminh_lsv;

	hdmi_write_reg(av_base, HDMI_CORE_AV_AUD_DBYTE(5), 0x00);
	hdmi_write_reg(av_base, HDMI_CORE_AV_AUD_DBYTE(6), 0x00);
	hdmi_write_reg(av_base, HDMI_CORE_AV_AUD_DBYTE(7), 0x00);
	hdmi_write_reg(av_base, HDMI_CORE_AV_AUD_DBYTE(8), 0x00);
	hdmi_write_reg(av_base, HDMI_CORE_AV_AUD_DBYTE(9), 0x00);

	checksum = 0x100 - sum;
	hdmi_write_reg(av_base,
					HDMI_CORE_AV_AUDIO_CHSUM, checksum);

	/*
	 * TODO: Add MPEG and SPD enable and repeat cfg when EDID parsing
	 * is available.
	 */
}

int hdmi4_audio_config(struct hdmi_core_data *core, struct hdmi_wp_data *wp,
		struct omap_dss_audio *audio, u32 pclk)
{
	struct hdmi_audio_format audio_format;
	struct hdmi_audio_dma audio_dma;
	struct hdmi_core_audio_config acore;
	int n, cts, channel_count;
	unsigned int fs_nr;
	bool word_length_16b = false;

	if (!audio || !audio->iec || !audio->cea || !core)
		return -EINVAL;

	acore.iec60958_cfg = audio->iec;
	/*
	 * In the IEC-60958 status word, check if the audio sample word length
	 * is 16-bit as several optimizations can be performed in such case.
	 */
	if (!(audio->iec->status[4] & IEC958_AES4_CON_MAX_WORDLEN_24))
		if (audio->iec->status[4] & IEC958_AES4_CON_WORDLEN_20_16)
			word_length_16b = true;

	/* I2S configuration. See Phillips' specification */
	if (word_length_16b)
		acore.i2s_cfg.justification = HDMI_AUDIO_JUSTIFY_LEFT;
	else
		acore.i2s_cfg.justification = HDMI_AUDIO_JUSTIFY_RIGHT;
	/*
	 * The I2S input word length is twice the length given in the IEC-60958
	 * status word. If the word size is greater than
	 * 20 bits, increment by one.
	 */
	acore.i2s_cfg.in_length_bits = audio->iec->status[4]
		& IEC958_AES4_CON_WORDLEN;
	if (audio->iec->status[4] & IEC958_AES4_CON_MAX_WORDLEN_24)
		acore.i2s_cfg.in_length_bits++;
	acore.i2s_cfg.sck_edge_mode = HDMI_AUDIO_I2S_SCK_EDGE_RISING;
	acore.i2s_cfg.vbit = HDMI_AUDIO_I2S_VBIT_FOR_PCM;
	acore.i2s_cfg.direction = HDMI_AUDIO_I2S_MSB_SHIFTED_FIRST;
	acore.i2s_cfg.shift = HDMI_AUDIO_I2S_FIRST_BIT_SHIFT;

	/* convert sample frequency to a number */
	switch (audio->iec->status[3] & IEC958_AES3_CON_FS) {
	case IEC958_AES3_CON_FS_32000:
		fs_nr = 32000;
		break;
	case IEC958_AES3_CON_FS_44100:
		fs_nr = 44100;
		break;
	case IEC958_AES3_CON_FS_48000:
		fs_nr = 48000;
		break;
	case IEC958_AES3_CON_FS_88200:
		fs_nr = 88200;
		break;
	case IEC958_AES3_CON_FS_96000:
		fs_nr = 96000;
		break;
	case IEC958_AES3_CON_FS_176400:
		fs_nr = 176400;
		break;
	case IEC958_AES3_CON_FS_192000:
		fs_nr = 192000;
		break;
	default:
		return -EINVAL;
	}

	hdmi_compute_acr(pclk, fs_nr, &n, &cts);

	/* Audio clock regeneration settings */
	acore.n = n;
	acore.cts = cts;
	if (dss_has_feature(FEAT_HDMI_CTS_SWMODE)) {
		acore.aud_par_busclk = 0;
		acore.cts_mode = HDMI_AUDIO_CTS_MODE_SW;
		acore.use_mclk = dss_has_feature(FEAT_HDMI_AUDIO_USE_MCLK);
	} else {
		acore.aud_par_busclk = (((128 * 31) - 1) << 8);
		acore.cts_mode = HDMI_AUDIO_CTS_MODE_HW;
		acore.use_mclk = true;
	}

	if (acore.use_mclk)
		acore.mclk_mode = HDMI_AUDIO_MCLK_128FS;

	/* Audio channels settings */
	channel_count = (audio->cea->db1_ct_cc &
			 CEA861_AUDIO_INFOFRAME_DB1CC) + 1;

	switch (channel_count) {
	case 2:
		audio_format.active_chnnls_msk = 0x03;
		break;
	case 3:
		audio_format.active_chnnls_msk = 0x07;
		break;
	case 4:
		audio_format.active_chnnls_msk = 0x0f;
		break;
	case 5:
		audio_format.active_chnnls_msk = 0x1f;
		break;
	case 6:
		audio_format.active_chnnls_msk = 0x3f;
		break;
	case 7:
		audio_format.active_chnnls_msk = 0x7f;
		break;
	case 8:
		audio_format.active_chnnls_msk = 0xff;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * the HDMI IP needs to enable four stereo channels when transmitting
	 * more than 2 audio channels.  Similarly, the channel count in the
	 * Audio InfoFrame has to match the sample_present bits (some channels
	 * are padded with zeroes)
	 */
	if (channel_count == 2) {
		audio_format.stereo_channels = HDMI_AUDIO_STEREO_ONECHANNEL;
		acore.i2s_cfg.active_sds = HDMI_AUDIO_I2S_SD0_EN;
		acore.layout = HDMI_AUDIO_LAYOUT_2CH;
	} else {
		audio_format.stereo_channels = HDMI_AUDIO_STEREO_FOURCHANNELS;
		acore.i2s_cfg.active_sds = HDMI_AUDIO_I2S_SD0_EN |
				HDMI_AUDIO_I2S_SD1_EN | HDMI_AUDIO_I2S_SD2_EN |
				HDMI_AUDIO_I2S_SD3_EN;
		acore.layout = HDMI_AUDIO_LAYOUT_8CH;
		audio->cea->db1_ct_cc = 7;
	}

	acore.en_spdif = false;
	/* use sample frequency from channel status word */
	acore.fs_override = true;
	/* enable ACR packets */
	acore.en_acr_pkt = true;
	/* disable direct streaming digital audio */
	acore.en_dsd_audio = false;
	/* use parallel audio interface */
	acore.en_parallel_aud_input = true;

	/* DMA settings */
	if (word_length_16b)
		audio_dma.transfer_size = 0x10;
	else
		audio_dma.transfer_size = 0x20;
	audio_dma.block_size = 0xC0;
	audio_dma.mode = HDMI_AUDIO_TRANSF_DMA;
	audio_dma.fifo_threshold = 0x20; /* in number of samples */

	/* audio FIFO format settings */
	if (word_length_16b) {
		audio_format.samples_per_word = HDMI_AUDIO_ONEWORD_TWOSAMPLES;
		audio_format.sample_size = HDMI_AUDIO_SAMPLE_16BITS;
		audio_format.justification = HDMI_AUDIO_JUSTIFY_LEFT;
	} else {
		audio_format.samples_per_word = HDMI_AUDIO_ONEWORD_ONESAMPLE;
		audio_format.sample_size = HDMI_AUDIO_SAMPLE_24BITS;
		audio_format.justification = HDMI_AUDIO_JUSTIFY_RIGHT;
	}
	audio_format.type = HDMI_AUDIO_TYPE_LPCM;
	audio_format.sample_order = HDMI_AUDIO_SAMPLE_LEFT_FIRST;
	/* disable start/stop signals of IEC 60958 blocks */
	audio_format.en_sig_blk_strt_end = HDMI_AUDIO_BLOCK_SIG_STARTEND_ON;

	/* configure DMA and audio FIFO format*/
	hdmi_wp_audio_config_dma(wp, &audio_dma);
	hdmi_wp_audio_config_format(wp, &audio_format);

	/* configure the core*/
	hdmi_core_audio_config(core, &acore);

	/* configure CEA 861 audio infoframe*/
	hdmi_core_audio_infoframe_cfg(core, audio->cea);

	return 0;
}

int hdmi4_audio_start(struct hdmi_core_data *core, struct hdmi_wp_data *wp)
{
	REG_FLD_MOD(hdmi_av_base(core),
		    HDMI_CORE_AV_AUD_MODE, true, 0, 0);

	hdmi_wp_audio_core_req_enable(wp, true);

	return 0;
}

void hdmi4_audio_stop(struct hdmi_core_data *core, struct hdmi_wp_data *wp)
{
	REG_FLD_MOD(hdmi_av_base(core),
		    HDMI_CORE_AV_AUD_MODE, false, 0, 0);

	hdmi_wp_audio_core_req_enable(wp, false);
}

int hdmi4_core_init(struct platform_device *pdev, struct hdmi_core_data *core)
{
	core->base = devm_platform_ioremap_resource_byname(pdev, "core");
	if (IS_ERR(core->base)) {
		DSSERR("can't ioremap CORE\n");
		return PTR_ERR(core->base);
	}

	return 0;
}
