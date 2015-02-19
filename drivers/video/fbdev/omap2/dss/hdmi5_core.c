/*
 * OMAP5 HDMI CORE IP driver library
 *
 * Copyright (C) 2014 Texas Instruments Incorporated
 *
 * Authors:
 *	Yong Zhi
 *	Mythri pk
 *	Archit Taneja <archit@ti.com>
 *	Tomi Valkeinen <tomi.valkeinen@ti.com>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include <drm/drm_edid.h>
#include <sound/asound.h>
#include <sound/asoundef.h>

#include "hdmi5_core.h"

/* only 24 bit color depth used for now */
static const struct csc_table csc_table_deepcolor[] = {
	/* HDMI_DEEP_COLOR_24BIT */
	[0] = { 7036, 0, 0, 32, 0, 7036, 0, 32, 0, 0, 7036, 32, },
	/* HDMI_DEEP_COLOR_30BIT */
	[1] = { 7015, 0, 0, 128, 0, 7015, 0, 128, 0, 0, 7015, 128, },
	/* HDMI_DEEP_COLOR_36BIT */
	[2] = { 7010, 0, 0, 512, 0, 7010, 0, 512, 0, 0, 7010, 512, },
	/* FULL RANGE */
	[3] = { 8192, 0, 0, 0, 0, 8192, 0, 0, 0, 0, 8192, 0, },
};

static void hdmi_core_ddc_init(struct hdmi_core_data *core)
{
	void __iomem *base = core->base;
	const unsigned long long iclk = 266000000;	/* DSS L3 ICLK */
	const unsigned ss_scl_high = 4000;		/* ns */
	const unsigned ss_scl_low = 4700;		/* ns */
	const unsigned fs_scl_high = 600;		/* ns */
	const unsigned fs_scl_low = 1300;		/* ns */
	const unsigned sda_hold = 300;			/* ns */
	const unsigned sfr_div = 10;
	unsigned long long sfr;
	unsigned v;

	sfr = iclk / sfr_div;	/* SFR_DIV */
	sfr /= 1000;		/* SFR clock in kHz */

	/* Reset */
	REG_FLD_MOD(base, HDMI_CORE_I2CM_SOFTRSTZ, 0, 0, 0);
	if (hdmi_wait_for_bit_change(base, HDMI_CORE_I2CM_SOFTRSTZ,
				0, 0, 1) != 1)
		DSSERR("HDMI I2CM reset failed\n");

	/* Standard (0) or Fast (1) Mode */
	REG_FLD_MOD(base, HDMI_CORE_I2CM_DIV, 0, 3, 3);

	/* Standard Mode SCL High counter */
	v = DIV_ROUND_UP_ULL(ss_scl_high * sfr, 1000000);
	REG_FLD_MOD(base, HDMI_CORE_I2CM_SS_SCL_HCNT_1_ADDR,
			(v >> 8) & 0xff, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_I2CM_SS_SCL_HCNT_0_ADDR,
			v & 0xff, 7, 0);

	/* Standard Mode SCL Low counter */
	v = DIV_ROUND_UP_ULL(ss_scl_low * sfr, 1000000);
	REG_FLD_MOD(base, HDMI_CORE_I2CM_SS_SCL_LCNT_1_ADDR,
			(v >> 8) & 0xff, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_I2CM_SS_SCL_LCNT_0_ADDR,
			v & 0xff, 7, 0);

	/* Fast Mode SCL High Counter */
	v = DIV_ROUND_UP_ULL(fs_scl_high * sfr, 1000000);
	REG_FLD_MOD(base, HDMI_CORE_I2CM_FS_SCL_HCNT_1_ADDR,
			(v >> 8) & 0xff, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_I2CM_FS_SCL_HCNT_0_ADDR,
			v & 0xff, 7, 0);

	/* Fast Mode SCL Low Counter */
	v = DIV_ROUND_UP_ULL(fs_scl_low * sfr, 1000000);
	REG_FLD_MOD(base, HDMI_CORE_I2CM_FS_SCL_LCNT_1_ADDR,
			(v >> 8) & 0xff, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_I2CM_FS_SCL_LCNT_0_ADDR,
			v & 0xff, 7, 0);

	/* SDA Hold Time */
	v = DIV_ROUND_UP_ULL(sda_hold * sfr, 1000000);
	REG_FLD_MOD(base, HDMI_CORE_I2CM_SDA_HOLD_ADDR, v & 0xff, 7, 0);

	REG_FLD_MOD(base, HDMI_CORE_I2CM_SLAVE, 0x50, 6, 0);
	REG_FLD_MOD(base, HDMI_CORE_I2CM_SEGADDR, 0x30, 6, 0);

	/* NACK_POL to high */
	REG_FLD_MOD(base, HDMI_CORE_I2CM_CTLINT, 0x1, 7, 7);

	/* NACK_MASK to unmasked */
	REG_FLD_MOD(base, HDMI_CORE_I2CM_CTLINT, 0x0, 6, 6);

	/* ARBITRATION_POL to high */
	REG_FLD_MOD(base, HDMI_CORE_I2CM_CTLINT, 0x1, 3, 3);

	/* ARBITRATION_MASK to unmasked */
	REG_FLD_MOD(base, HDMI_CORE_I2CM_CTLINT, 0x0, 2, 2);

	/* DONE_POL to high */
	REG_FLD_MOD(base, HDMI_CORE_I2CM_INT, 0x1, 3, 3);

	/* DONE_MASK to unmasked */
	REG_FLD_MOD(base, HDMI_CORE_I2CM_INT, 0x0, 2, 2);
}

static void hdmi_core_ddc_uninit(struct hdmi_core_data *core)
{
	void __iomem *base = core->base;

	/* Mask I2C interrupts */
	REG_FLD_MOD(base, HDMI_CORE_I2CM_CTLINT, 0x1, 6, 6);
	REG_FLD_MOD(base, HDMI_CORE_I2CM_CTLINT, 0x1, 2, 2);
	REG_FLD_MOD(base, HDMI_CORE_I2CM_INT, 0x1, 2, 2);
}

static int hdmi_core_ddc_edid(struct hdmi_core_data *core, u8 *pedid, u8 ext)
{
	void __iomem *base = core->base;
	u8 cur_addr;
	char checksum = 0;
	const int retries = 1000;
	u8 seg_ptr = ext / 2;
	u8 edidbase = ((ext % 2) * 0x80);

	REG_FLD_MOD(base, HDMI_CORE_I2CM_SEGPTR, seg_ptr, 7, 0);

	/*
	 * TODO: We use polling here, although we probably should use proper
	 * interrupts.
	 */
	for (cur_addr = 0; cur_addr < 128; ++cur_addr) {
		int i;

		/* clear ERROR and DONE */
		REG_FLD_MOD(base, HDMI_CORE_IH_I2CM_STAT0, 0x3, 1, 0);

		REG_FLD_MOD(base, HDMI_CORE_I2CM_ADDRESS,
				edidbase + cur_addr, 7, 0);

		if (seg_ptr)
			REG_FLD_MOD(base, HDMI_CORE_I2CM_OPERATION, 1, 1, 1);
		else
			REG_FLD_MOD(base, HDMI_CORE_I2CM_OPERATION, 1, 0, 0);

		for (i = 0; i < retries; ++i) {
			u32 stat;

			stat = REG_GET(base, HDMI_CORE_IH_I2CM_STAT0, 1, 0);

			/* I2CM_ERROR */
			if (stat & 1) {
				DSSERR("HDMI I2C Master Error\n");
				return -EIO;
			}

			/* I2CM_DONE */
			if (stat & (1 << 1))
				break;

			usleep_range(250, 1000);
		}

		if (i == retries) {
			DSSERR("HDMI I2C timeout reading EDID\n");
			return -EIO;
		}

		pedid[cur_addr] = REG_GET(base, HDMI_CORE_I2CM_DATAI, 7, 0);
		checksum += pedid[cur_addr];
	}

	return 0;

}

int hdmi5_read_edid(struct hdmi_core_data *core, u8 *edid, int len)
{
	int r, n, i;
	int max_ext_blocks = (len / 128) - 1;

	if (len < 128)
		return -EINVAL;

	hdmi_core_ddc_init(core);

	r = hdmi_core_ddc_edid(core, edid, 0);
	if (r)
		goto out;

	n = edid[0x7e];

	if (n > max_ext_blocks)
		n = max_ext_blocks;

	for (i = 1; i <= n; i++) {
		r = hdmi_core_ddc_edid(core, edid + i * EDID_LENGTH, i);
		if (r)
			goto out;
	}

out:
	hdmi_core_ddc_uninit(core);

	return r ? r : len;
}

void hdmi5_core_dump(struct hdmi_core_data *core, struct seq_file *s)
{

#define DUMPCORE(r) seq_printf(s, "%-35s %08x\n", #r,\
		hdmi_read_reg(core->base, r))

	DUMPCORE(HDMI_CORE_FC_INVIDCONF);
	DUMPCORE(HDMI_CORE_FC_INHACTIV0);
	DUMPCORE(HDMI_CORE_FC_INHACTIV1);
	DUMPCORE(HDMI_CORE_FC_INHBLANK0);
	DUMPCORE(HDMI_CORE_FC_INHBLANK1);
	DUMPCORE(HDMI_CORE_FC_INVACTIV0);
	DUMPCORE(HDMI_CORE_FC_INVACTIV1);
	DUMPCORE(HDMI_CORE_FC_INVBLANK);
	DUMPCORE(HDMI_CORE_FC_HSYNCINDELAY0);
	DUMPCORE(HDMI_CORE_FC_HSYNCINDELAY1);
	DUMPCORE(HDMI_CORE_FC_HSYNCINWIDTH0);
	DUMPCORE(HDMI_CORE_FC_HSYNCINWIDTH1);
	DUMPCORE(HDMI_CORE_FC_VSYNCINDELAY);
	DUMPCORE(HDMI_CORE_FC_VSYNCINWIDTH);
	DUMPCORE(HDMI_CORE_FC_CTRLDUR);
	DUMPCORE(HDMI_CORE_FC_EXCTRLDUR);
	DUMPCORE(HDMI_CORE_FC_EXCTRLSPAC);
	DUMPCORE(HDMI_CORE_FC_CH0PREAM);
	DUMPCORE(HDMI_CORE_FC_CH1PREAM);
	DUMPCORE(HDMI_CORE_FC_CH2PREAM);
	DUMPCORE(HDMI_CORE_FC_AVICONF0);
	DUMPCORE(HDMI_CORE_FC_AVICONF1);
	DUMPCORE(HDMI_CORE_FC_AVICONF2);
	DUMPCORE(HDMI_CORE_FC_AVIVID);
	DUMPCORE(HDMI_CORE_FC_PRCONF);

	DUMPCORE(HDMI_CORE_MC_CLKDIS);
	DUMPCORE(HDMI_CORE_MC_SWRSTZREQ);
	DUMPCORE(HDMI_CORE_MC_FLOWCTRL);
	DUMPCORE(HDMI_CORE_MC_PHYRSTZ);
	DUMPCORE(HDMI_CORE_MC_LOCKONCLOCK);

	DUMPCORE(HDMI_CORE_I2CM_SLAVE);
	DUMPCORE(HDMI_CORE_I2CM_ADDRESS);
	DUMPCORE(HDMI_CORE_I2CM_DATAO);
	DUMPCORE(HDMI_CORE_I2CM_DATAI);
	DUMPCORE(HDMI_CORE_I2CM_OPERATION);
	DUMPCORE(HDMI_CORE_I2CM_INT);
	DUMPCORE(HDMI_CORE_I2CM_CTLINT);
	DUMPCORE(HDMI_CORE_I2CM_DIV);
	DUMPCORE(HDMI_CORE_I2CM_SEGADDR);
	DUMPCORE(HDMI_CORE_I2CM_SOFTRSTZ);
	DUMPCORE(HDMI_CORE_I2CM_SEGPTR);
	DUMPCORE(HDMI_CORE_I2CM_SS_SCL_HCNT_1_ADDR);
	DUMPCORE(HDMI_CORE_I2CM_SS_SCL_HCNT_0_ADDR);
	DUMPCORE(HDMI_CORE_I2CM_SS_SCL_LCNT_1_ADDR);
	DUMPCORE(HDMI_CORE_I2CM_SS_SCL_LCNT_0_ADDR);
	DUMPCORE(HDMI_CORE_I2CM_FS_SCL_HCNT_1_ADDR);
	DUMPCORE(HDMI_CORE_I2CM_FS_SCL_HCNT_0_ADDR);
	DUMPCORE(HDMI_CORE_I2CM_FS_SCL_LCNT_1_ADDR);
	DUMPCORE(HDMI_CORE_I2CM_FS_SCL_LCNT_0_ADDR);
	DUMPCORE(HDMI_CORE_I2CM_SDA_HOLD_ADDR);
}

static void hdmi_core_init(struct hdmi_core_vid_config *video_cfg,
			struct hdmi_config *cfg)
{
	DSSDBG("hdmi_core_init\n");

	/* video core */
	video_cfg->data_enable_pol = 1; /* It is always 1*/
	video_cfg->v_fc_config.timings.hsync_level = cfg->timings.hsync_level;
	video_cfg->v_fc_config.timings.x_res = cfg->timings.x_res;
	video_cfg->v_fc_config.timings.hsw = cfg->timings.hsw - 1;
	video_cfg->v_fc_config.timings.hbp = cfg->timings.hbp;
	video_cfg->v_fc_config.timings.hfp = cfg->timings.hfp;
	video_cfg->hblank = cfg->timings.hfp +
				cfg->timings.hbp + cfg->timings.hsw - 1;
	video_cfg->v_fc_config.timings.vsync_level = cfg->timings.vsync_level;
	video_cfg->v_fc_config.timings.y_res = cfg->timings.y_res;
	video_cfg->v_fc_config.timings.vsw = cfg->timings.vsw;
	video_cfg->v_fc_config.timings.vfp = cfg->timings.vfp;
	video_cfg->v_fc_config.timings.vbp = cfg->timings.vbp;
	video_cfg->vblank_osc = 0; /* Always 0 - need to confirm */
	video_cfg->vblank = cfg->timings.vsw +
				cfg->timings.vfp + cfg->timings.vbp;
	video_cfg->v_fc_config.hdmi_dvi_mode = cfg->hdmi_dvi_mode;
	video_cfg->v_fc_config.timings.interlace = cfg->timings.interlace;
}

/* DSS_HDMI_CORE_VIDEO_CONFIG */
static void hdmi_core_video_config(struct hdmi_core_data *core,
			struct hdmi_core_vid_config *cfg)
{
	void __iomem *base = core->base;
	unsigned char r = 0;
	bool vsync_pol, hsync_pol;

	vsync_pol =
		cfg->v_fc_config.timings.vsync_level == OMAPDSS_SIG_ACTIVE_HIGH;
	hsync_pol =
		cfg->v_fc_config.timings.hsync_level == OMAPDSS_SIG_ACTIVE_HIGH;

	/* Set hsync, vsync and data-enable polarity  */
	r = hdmi_read_reg(base, HDMI_CORE_FC_INVIDCONF);
	r = FLD_MOD(r, vsync_pol, 6, 6);
	r = FLD_MOD(r, hsync_pol, 5, 5);
	r = FLD_MOD(r, cfg->data_enable_pol, 4, 4);
	r = FLD_MOD(r, cfg->vblank_osc, 1, 1);
	r = FLD_MOD(r, cfg->v_fc_config.timings.interlace, 0, 0);
	hdmi_write_reg(base, HDMI_CORE_FC_INVIDCONF, r);

	/* set x resolution */
	REG_FLD_MOD(base, HDMI_CORE_FC_INHACTIV1,
			cfg->v_fc_config.timings.x_res >> 8, 4, 0);
	REG_FLD_MOD(base, HDMI_CORE_FC_INHACTIV0,
			cfg->v_fc_config.timings.x_res & 0xFF, 7, 0);

	/* set y resolution */
	REG_FLD_MOD(base, HDMI_CORE_FC_INVACTIV1,
			cfg->v_fc_config.timings.y_res >> 8, 4, 0);
	REG_FLD_MOD(base, HDMI_CORE_FC_INVACTIV0,
			cfg->v_fc_config.timings.y_res & 0xFF, 7, 0);

	/* set horizontal blanking pixels */
	REG_FLD_MOD(base, HDMI_CORE_FC_INHBLANK1, cfg->hblank >> 8, 4, 0);
	REG_FLD_MOD(base, HDMI_CORE_FC_INHBLANK0, cfg->hblank & 0xFF, 7, 0);

	/* set vertial blanking pixels */
	REG_FLD_MOD(base, HDMI_CORE_FC_INVBLANK, cfg->vblank, 7, 0);

	/* set horizontal sync offset */
	REG_FLD_MOD(base, HDMI_CORE_FC_HSYNCINDELAY1,
			cfg->v_fc_config.timings.hfp >> 8, 4, 0);
	REG_FLD_MOD(base, HDMI_CORE_FC_HSYNCINDELAY0,
			cfg->v_fc_config.timings.hfp & 0xFF, 7, 0);

	/* set vertical sync offset */
	REG_FLD_MOD(base, HDMI_CORE_FC_VSYNCINDELAY,
			cfg->v_fc_config.timings.vfp, 7, 0);

	/* set horizontal sync pulse width */
	REG_FLD_MOD(base, HDMI_CORE_FC_HSYNCINWIDTH1,
			(cfg->v_fc_config.timings.hsw >> 8), 1, 0);
	REG_FLD_MOD(base, HDMI_CORE_FC_HSYNCINWIDTH0,
			cfg->v_fc_config.timings.hsw & 0xFF, 7, 0);

	/*  set vertical sync pulse width */
	REG_FLD_MOD(base, HDMI_CORE_FC_VSYNCINWIDTH,
			cfg->v_fc_config.timings.vsw, 5, 0);

	/* select DVI mode */
	REG_FLD_MOD(base, HDMI_CORE_FC_INVIDCONF,
			cfg->v_fc_config.hdmi_dvi_mode, 3, 3);
}

static void hdmi_core_config_video_packetizer(struct hdmi_core_data *core)
{
	void __iomem *base = core->base;
	int clr_depth = 0;	/* 24 bit color depth */

	/* COLOR_DEPTH */
	REG_FLD_MOD(base, HDMI_CORE_VP_PR_CD, clr_depth, 7, 4);
	/* BYPASS_EN */
	REG_FLD_MOD(base, HDMI_CORE_VP_CONF, clr_depth ? 0 : 1, 6, 6);
	/* PP_EN */
	REG_FLD_MOD(base, HDMI_CORE_VP_CONF, clr_depth ? 1 : 0, 5, 5);
	/* YCC422_EN */
	REG_FLD_MOD(base, HDMI_CORE_VP_CONF, 0, 3, 3);
	/* PP_STUFFING */
	REG_FLD_MOD(base, HDMI_CORE_VP_STUFF, clr_depth ? 1 : 0, 1, 1);
	/* YCC422_STUFFING */
	REG_FLD_MOD(base, HDMI_CORE_VP_STUFF, 1, 2, 2);
	/* OUTPUT_SELECTOR */
	REG_FLD_MOD(base, HDMI_CORE_VP_CONF, clr_depth ? 0 : 2, 1, 0);
}

static void hdmi_core_config_csc(struct hdmi_core_data *core)
{
	int clr_depth = 0;	/* 24 bit color depth */

	/* CSC_COLORDEPTH */
	REG_FLD_MOD(core->base, HDMI_CORE_CSC_SCALE, clr_depth, 7, 4);
}

static void hdmi_core_config_video_sampler(struct hdmi_core_data *core)
{
	int video_mapping = 1;	/* for 24 bit color depth */

	/* VIDEO_MAPPING */
	REG_FLD_MOD(core->base, HDMI_CORE_TX_INVID0, video_mapping, 4, 0);
}

static void hdmi_core_write_avi_infoframe(struct hdmi_core_data *core,
	struct hdmi_avi_infoframe *frame)
{
	void __iomem *base = core->base;
	u8 data[HDMI_INFOFRAME_SIZE(AVI)];
	u8 *ptr;
	unsigned y, a, b, s;
	unsigned c, m, r;
	unsigned itc, ec, q, sc;
	unsigned vic;
	unsigned yq, cn, pr;

	hdmi_avi_infoframe_pack(frame, data, sizeof(data));

	print_hex_dump_debug("AVI: ", DUMP_PREFIX_NONE, 16, 1, data,
		HDMI_INFOFRAME_SIZE(AVI), false);

	ptr = data + HDMI_INFOFRAME_HEADER_SIZE;

	y = (ptr[0] >> 5) & 0x3;
	a = (ptr[0] >> 4) & 0x1;
	b = (ptr[0] >> 2) & 0x3;
	s = (ptr[0] >> 0) & 0x3;

	c = (ptr[1] >> 6) & 0x3;
	m = (ptr[1] >> 4) & 0x3;
	r = (ptr[1] >> 0) & 0x3;

	itc = (ptr[2] >> 7) & 0x1;
	ec = (ptr[2] >> 4) & 0x7;
	q = (ptr[2] >> 2) & 0x3;
	sc = (ptr[2] >> 0) & 0x3;

	vic = ptr[3];

	yq = (ptr[4] >> 6) & 0x3;
	cn = (ptr[4] >> 4) & 0x3;
	pr = (ptr[4] >> 0) & 0xf;

	hdmi_write_reg(base, HDMI_CORE_FC_AVICONF0,
		(a << 6) | (s << 4) | (b << 2) | (y << 0));

	hdmi_write_reg(base, HDMI_CORE_FC_AVICONF1,
		(c << 6) | (m << 4) | (r << 0));

	hdmi_write_reg(base, HDMI_CORE_FC_AVICONF2,
		(itc << 7) | (ec << 4) | (q << 2) | (sc << 0));

	hdmi_write_reg(base, HDMI_CORE_FC_AVIVID, vic);

	hdmi_write_reg(base, HDMI_CORE_FC_AVICONF3,
		(yq << 2) | (cn << 0));

	REG_FLD_MOD(base, HDMI_CORE_FC_PRCONF, pr, 3, 0);
}

static void hdmi_core_csc_config(struct hdmi_core_data *core,
		struct csc_table csc_coeff)
{
	void __iomem *base = core->base;

	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_A1_MSB, csc_coeff.a1 >> 8 , 6, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_A1_LSB, csc_coeff.a1, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_A2_MSB, csc_coeff.a2 >> 8, 6, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_A2_LSB, csc_coeff.a2, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_A3_MSB, csc_coeff.a3 >> 8, 6, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_A3_LSB, csc_coeff.a3, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_A4_MSB, csc_coeff.a4 >> 8, 6, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_A4_LSB, csc_coeff.a4, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_B1_MSB, csc_coeff.b1 >> 8, 6, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_B1_LSB, csc_coeff.b1, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_B2_MSB, csc_coeff.b2 >> 8, 6, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_B2_LSB, csc_coeff.b2, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_B3_MSB, csc_coeff.b3 >> 8, 6, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_B3_LSB, csc_coeff.b3, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_B4_MSB, csc_coeff.b4 >> 8, 6, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_B4_LSB, csc_coeff.b4, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_C1_MSB, csc_coeff.c1 >> 8, 6, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_C1_LSB, csc_coeff.c1, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_C2_MSB, csc_coeff.c2 >> 8, 6, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_C2_LSB, csc_coeff.c2, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_C3_MSB, csc_coeff.c3 >> 8, 6, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_C3_LSB, csc_coeff.c3, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_C4_MSB, csc_coeff.c4 >> 8, 6, 0);
	REG_FLD_MOD(base, HDMI_CORE_CSC_COEF_C4_LSB, csc_coeff.c4, 7, 0);

	REG_FLD_MOD(base, HDMI_CORE_MC_FLOWCTRL, 0x1, 0, 0);
}

static void hdmi_core_configure_range(struct hdmi_core_data *core)
{
	struct csc_table csc_coeff = { 0 };

	/* support limited range with 24 bit color depth for now */
	csc_coeff = csc_table_deepcolor[0];

	hdmi_core_csc_config(core, csc_coeff);
}

static void hdmi_core_enable_video_path(struct hdmi_core_data *core)
{
	void __iomem *base = core->base;

	DSSDBG("hdmi_core_enable_video_path\n");

	REG_FLD_MOD(base, HDMI_CORE_FC_CTRLDUR, 0x0C, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_FC_EXCTRLDUR, 0x20, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_FC_EXCTRLSPAC, 0x01, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_FC_CH0PREAM, 0x0B, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_FC_CH1PREAM, 0x16, 5, 0);
	REG_FLD_MOD(base, HDMI_CORE_FC_CH2PREAM, 0x21, 5, 0);
	REG_FLD_MOD(base, HDMI_CORE_MC_CLKDIS, 0x00, 0, 0);
	REG_FLD_MOD(base, HDMI_CORE_MC_CLKDIS, 0x00, 1, 1);
}

static void hdmi_core_mask_interrupts(struct hdmi_core_data *core)
{
	void __iomem *base = core->base;

	/* Master IRQ mask */
	REG_FLD_MOD(base, HDMI_CORE_IH_MUTE, 0x3, 1, 0);

	/* Mask all the interrupts in HDMI core */

	REG_FLD_MOD(base, HDMI_CORE_VP_MASK, 0xff, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_FC_MASK0, 0xe7, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_FC_MASK1, 0xfb, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_FC_MASK2, 0x3, 1, 0);

	REG_FLD_MOD(base, HDMI_CORE_AUD_INT, 0x3, 3, 2);
	REG_FLD_MOD(base, HDMI_CORE_AUD_GP_MASK, 0x3, 1, 0);

	REG_FLD_MOD(base, HDMI_CORE_CEC_MASK, 0x7f, 6, 0);

	REG_FLD_MOD(base, HDMI_CORE_I2CM_CTLINT, 0x1, 6, 6);
	REG_FLD_MOD(base, HDMI_CORE_I2CM_CTLINT, 0x1, 2, 2);
	REG_FLD_MOD(base, HDMI_CORE_I2CM_INT, 0x1, 2, 2);

	REG_FLD_MOD(base, HDMI_CORE_PHY_MASK0, 0xf3, 7, 0);

	REG_FLD_MOD(base, HDMI_CORE_IH_PHY_STAT0, 0xff, 7, 0);

	/* Clear all the current interrupt bits */

	REG_FLD_MOD(base, HDMI_CORE_IH_VP_STAT0, 0xff, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_IH_FC_STAT0, 0xe7, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_IH_FC_STAT1, 0xfb, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_IH_FC_STAT2, 0x3, 1, 0);

	REG_FLD_MOD(base, HDMI_CORE_IH_AS_STAT0, 0x7, 2, 0);

	REG_FLD_MOD(base, HDMI_CORE_IH_CEC_STAT0, 0x7f, 6, 0);

	REG_FLD_MOD(base, HDMI_CORE_IH_I2CM_STAT0, 0x3, 1, 0);

	REG_FLD_MOD(base, HDMI_CORE_IH_PHY_STAT0, 0xff, 7, 0);
}

static void hdmi_core_enable_interrupts(struct hdmi_core_data *core)
{
	/* Unmute interrupts */
	REG_FLD_MOD(core->base, HDMI_CORE_IH_MUTE, 0x0, 1, 0);
}

int hdmi5_core_handle_irqs(struct hdmi_core_data *core)
{
	void __iomem *base = core->base;

	REG_FLD_MOD(base, HDMI_CORE_IH_FC_STAT0, 0xff, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_IH_FC_STAT1, 0xff, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_IH_FC_STAT2, 0xff, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_IH_AS_STAT0, 0xff, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_IH_PHY_STAT0, 0xff, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_IH_I2CM_STAT0, 0xff, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_IH_CEC_STAT0, 0xff, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_IH_VP_STAT0, 0xff, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_IH_I2CMPHY_STAT0, 0xff, 7, 0);

	return 0;
}

void hdmi5_configure(struct hdmi_core_data *core, struct hdmi_wp_data *wp,
		struct hdmi_config *cfg)
{
	struct omap_video_timings video_timing;
	struct hdmi_video_format video_format;
	struct hdmi_core_vid_config v_core_cfg;

	hdmi_core_mask_interrupts(core);

	hdmi_core_init(&v_core_cfg, cfg);

	hdmi_wp_init_vid_fmt_timings(&video_format, &video_timing, cfg);

	hdmi_wp_video_config_timing(wp, &video_timing);

	/* video config */
	video_format.packing_mode = HDMI_PACK_24b_RGB_YUV444_YUV422;

	hdmi_wp_video_config_format(wp, &video_format);

	hdmi_wp_video_config_interface(wp, &video_timing);

	/* support limited range with 24 bit color depth for now */
	hdmi_core_configure_range(core);
	cfg->infoframe.quantization_range = HDMI_QUANTIZATION_RANGE_LIMITED;

	/*
	 * configure core video part, set software reset in the core
	 */
	v_core_cfg.packet_mode = HDMI_PACKETMODE24BITPERPIXEL;

	hdmi_core_video_config(core, &v_core_cfg);

	hdmi_core_config_video_packetizer(core);
	hdmi_core_config_csc(core);
	hdmi_core_config_video_sampler(core);

	if (cfg->hdmi_dvi_mode == HDMI_HDMI)
		hdmi_core_write_avi_infoframe(core, &cfg->infoframe);

	hdmi_core_enable_video_path(core);

	hdmi_core_enable_interrupts(core);
}

static void hdmi5_core_audio_config(struct hdmi_core_data *core,
			struct hdmi_core_audio_config *cfg)
{
	void __iomem *base = core->base;
	u8 val;

	/* Mute audio before configuring */
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSCONF, 0xf, 7, 4);

	/* Set the N parameter */
	REG_FLD_MOD(base, HDMI_CORE_AUD_N1, cfg->n, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_AUD_N2, cfg->n >> 8, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_AUD_N3, cfg->n >> 16, 3, 0);

	/*
	 * CTS manual mode. Automatic mode is not supported when using audio
	 * parallel interface.
	 */
	REG_FLD_MOD(base, HDMI_CORE_AUD_CTS3, 1, 4, 4);
	REG_FLD_MOD(base, HDMI_CORE_AUD_CTS1, cfg->cts, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_AUD_CTS2, cfg->cts >> 8, 7, 0);
	REG_FLD_MOD(base, HDMI_CORE_AUD_CTS3, cfg->cts >> 16, 3, 0);

	/* Layout of Audio Sample Packets: 2-channel or multichannels */
	if (cfg->layout == HDMI_AUDIO_LAYOUT_2CH)
		REG_FLD_MOD(base, HDMI_CORE_FC_AUDSCONF, 0, 0, 0);
	else
		REG_FLD_MOD(base, HDMI_CORE_FC_AUDSCONF, 1, 0, 0);

	/* Configure IEC-609580 Validity bits */
	/* Channel 0 is valid */
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSV, 0, 0, 0);
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSV, 0, 4, 4);

	if (cfg->layout == HDMI_AUDIO_LAYOUT_2CH)
		val = 1;
	else
		val = 0;

	/* Channels 1, 2 setting */
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSV, val, 1, 1);
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSV, val, 5, 5);
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSV, val, 2, 2);
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSV, val, 6, 6);
	/* Channel 3 setting */
	if (cfg->layout == HDMI_AUDIO_LAYOUT_6CH)
		val = 1;
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSV, val, 3, 3);
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSV, val, 7, 7);

	/* Configure IEC-60958 User bits */
	/* TODO: should be set by user. */
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSU, 0, 7, 0);

	/* Configure IEC-60958 Channel Status word */
	/* CGMSA */
	val = cfg->iec60958_cfg->status[5] & IEC958_AES5_CON_CGMSA;
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSCHNLS(0), val, 5, 4);

	/* Copyright */
	val = (cfg->iec60958_cfg->status[0] &
			IEC958_AES0_CON_NOT_COPYRIGHT) >> 2;
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSCHNLS(0), val, 0, 0);

	/* Category */
	hdmi_write_reg(base, HDMI_CORE_FC_AUDSCHNLS(1),
		cfg->iec60958_cfg->status[1]);

	/* PCM audio mode */
	val = (cfg->iec60958_cfg->status[0] & IEC958_AES0_CON_MODE) >> 6;
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSCHNLS(2), val, 6, 4);

	/* Source number */
	val = cfg->iec60958_cfg->status[2] & IEC958_AES2_CON_SOURCE;
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSCHNLS(2), val, 3, 0);

	/* Channel number right 0  */
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSCHNLS(3), 2, 3, 0);
	/* Channel number right 1*/
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSCHNLS(3), 4, 7, 4);
	/* Channel number right 2  */
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSCHNLS(4), 6, 3, 0);
	/* Channel number right 3*/
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSCHNLS(4), 8, 7, 4);
	/* Channel number left 0  */
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSCHNLS(5), 1, 3, 0);
	/* Channel number left 1*/
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSCHNLS(5), 3, 7, 4);
	/* Channel number left 2  */
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSCHNLS(6), 5, 3, 0);
	/* Channel number left 3*/
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSCHNLS(6), 7, 7, 4);

	/* Clock accuracy and sample rate */
	hdmi_write_reg(base, HDMI_CORE_FC_AUDSCHNLS(7),
		cfg->iec60958_cfg->status[3]);

	/* Original sample rate and word length */
	hdmi_write_reg(base, HDMI_CORE_FC_AUDSCHNLS(8),
		cfg->iec60958_cfg->status[4]);

	/* Enable FIFO empty and full interrupts */
	REG_FLD_MOD(base, HDMI_CORE_AUD_INT, 3, 3, 2);

	/* Configure GPA */
	/* select HBR/SPDIF interfaces */
	if (cfg->layout == HDMI_AUDIO_LAYOUT_2CH) {
		/* select HBR/SPDIF interfaces */
		REG_FLD_MOD(base, HDMI_CORE_AUD_CONF0, 0, 5, 5);
		/* enable two channels in GPA */
		REG_FLD_MOD(base, HDMI_CORE_AUD_GP_CONF1, 3, 7, 0);
	} else if (cfg->layout == HDMI_AUDIO_LAYOUT_6CH) {
		/* select HBR/SPDIF interfaces */
		REG_FLD_MOD(base, HDMI_CORE_AUD_CONF0, 0, 5, 5);
		/* enable six channels in GPA */
		REG_FLD_MOD(base, HDMI_CORE_AUD_GP_CONF1, 0x3F, 7, 0);
	} else {
		/* select HBR/SPDIF interfaces */
		REG_FLD_MOD(base, HDMI_CORE_AUD_CONF0, 0, 5, 5);
		/* enable eight channels in GPA */
		REG_FLD_MOD(base, HDMI_CORE_AUD_GP_CONF1, 0xFF, 7, 0);
	}

	/* disable HBR */
	REG_FLD_MOD(base, HDMI_CORE_AUD_GP_CONF2, 0, 0, 0);
	/* enable PCUV */
	REG_FLD_MOD(base, HDMI_CORE_AUD_GP_CONF2, 1, 1, 1);
	/* enable GPA FIFO full and empty mask */
	REG_FLD_MOD(base, HDMI_CORE_AUD_GP_MASK, 3, 1, 0);
	/* set polarity of GPA FIFO empty interrupts */
	REG_FLD_MOD(base, HDMI_CORE_AUD_GP_POL, 1, 0, 0);

	/* unmute audio */
	REG_FLD_MOD(base, HDMI_CORE_FC_AUDSCONF, 0, 7, 4);
}

static void hdmi5_core_audio_infoframe_cfg(struct hdmi_core_data *core,
	 struct snd_cea_861_aud_if *info_aud)
{
	void __iomem *base = core->base;

	/* channel count and coding type fields in AUDICONF0 are swapped */
	hdmi_write_reg(base, HDMI_CORE_FC_AUDICONF0,
		(info_aud->db1_ct_cc & CEA861_AUDIO_INFOFRAME_DB1CC) << 4 |
		(info_aud->db1_ct_cc & CEA861_AUDIO_INFOFRAME_DB1CT) >> 4);

	hdmi_write_reg(base, HDMI_CORE_FC_AUDICONF1, info_aud->db2_sf_ss);
	hdmi_write_reg(base, HDMI_CORE_FC_AUDICONF2, info_aud->db4_ca);
	hdmi_write_reg(base, HDMI_CORE_FC_AUDICONF3, info_aud->db5_dminh_lsv);
}

int hdmi5_audio_config(struct hdmi_core_data *core, struct hdmi_wp_data *wp,
			struct omap_dss_audio *audio, u32 pclk)
{
	struct hdmi_audio_format audio_format;
	struct hdmi_audio_dma audio_dma;
	struct hdmi_core_audio_config core_cfg;
	int err, n, cts, channel_count;
	unsigned int fs_nr;
	bool word_length_16b = false;

	if (!audio || !audio->iec || !audio->cea || !core)
		return -EINVAL;

	core_cfg.iec60958_cfg = audio->iec;

	if (!(audio->iec->status[4] & IEC958_AES4_CON_MAX_WORDLEN_24) &&
		(audio->iec->status[4] & IEC958_AES4_CON_WORDLEN_20_16))
			word_length_16b = true;

	/* only 16-bit word length supported atm */
	if (!word_length_16b)
		return -EINVAL;

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

	err = hdmi_compute_acr(pclk, fs_nr, &n, &cts);
	core_cfg.n = n;
	core_cfg.cts = cts;

	/* Audio channels settings */
	channel_count = (audio->cea->db1_ct_cc & CEA861_AUDIO_INFOFRAME_DB1CC)
				+ 1;

	if (channel_count == 2)
		core_cfg.layout = HDMI_AUDIO_LAYOUT_2CH;
	else if (channel_count == 6)
		core_cfg.layout = HDMI_AUDIO_LAYOUT_6CH;
	else
		core_cfg.layout = HDMI_AUDIO_LAYOUT_8CH;

	/* DMA settings */
	if (word_length_16b)
		audio_dma.transfer_size = 0x10;
	else
		audio_dma.transfer_size = 0x20;
	audio_dma.block_size = 0xC0;
	audio_dma.mode = HDMI_AUDIO_TRANSF_DMA;
	audio_dma.fifo_threshold = 0x20; /* in number of samples */

	/* audio FIFO format settings for 16-bit samples*/
	audio_format.samples_per_word = HDMI_AUDIO_ONEWORD_TWOSAMPLES;
	audio_format.sample_size = HDMI_AUDIO_SAMPLE_16BITS;
	audio_format.justification = HDMI_AUDIO_JUSTIFY_LEFT;

	/* only LPCM atm */
	audio_format.type = HDMI_AUDIO_TYPE_LPCM;

	/* only allowed option */
	audio_format.sample_order = HDMI_AUDIO_SAMPLE_LEFT_FIRST;

	/* disable start/stop signals of IEC 60958 blocks */
	audio_format.en_sig_blk_strt_end = HDMI_AUDIO_BLOCK_SIG_STARTEND_ON;

	/* configure DMA and audio FIFO format*/
	hdmi_wp_audio_config_dma(wp, &audio_dma);
	hdmi_wp_audio_config_format(wp, &audio_format);

	/* configure the core */
	hdmi5_core_audio_config(core, &core_cfg);

	/* configure CEA 861 audio infoframe */
	hdmi5_core_audio_infoframe_cfg(core, audio->cea);

	return 0;
}

int hdmi5_core_init(struct platform_device *pdev, struct hdmi_core_data *core)
{
	struct resource *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "core");
	if (!res) {
		DSSERR("can't get CORE IORESOURCE_MEM HDMI\n");
		return -EINVAL;
	}

	core->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(core->base)) {
		DSSERR("can't ioremap HDMI core\n");
		return PTR_ERR(core->base);
	}

	return 0;
}
